/*
 * Race for the Galaxy AI
 * 
 * Copyright (C) 2009 Keldon Jones
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>
#include "rftg.h"
#include "client.h"
#include "comm.h"

/* Apple OS X specific-code */
#ifdef __APPLE__     
#include "CoreFoundation/CoreFoundation.h"
#include "ige-mac-menu.h"
#endif

/*
 * Our default options.
 */
options opt = 
{
	.num_players = 3,
};

/*
 * Keyfile with our preferences.
 */
static GKeyFile *pref_file;

/*
 * AI verbosity.
 */
int verbose = 0;

/*
 * Current (real) game state.
 */
game real_game;

/*
 * State at start of turn.
 */
static game save_state;
static game undo_state;

/*
 * Player we're playing as.
 */
int player_us;

/*
 * We have restarted the main game loop.
 */
int restart_loop;

/*
 * Player names.
 */
static char *player_names[MAX_PLAYER] =
{
	"Blue",
	"Red",
	"Green",
	"Yellow",
	"Cyan",
	"Purple",
};

/*
 * Player color names.
 */
static char *player_colors[MAX_PLAYER] =
{
	"#8888aa",
	"#aa8888",
	"#88aa88",
	"#aaaa88",
	"#88aaaa",
	"#aa88aa",
};

/*
 * Card image size.
 */
#define CARD_WIDTH 372
#define CARD_HEIGHT 520

/*
 * Goal image size.
 */
#define GOALF_WIDTH 260
#define GOALF_HEIGHT 297
#define GOALM_WIDTH 296
#define GOALM_HEIGHT 447

/*
 * Information about a displayed card.
 */
typedef struct displayed
{
	/* Card's index in the deck */
	int index;

	/* Card's design pointer */
	design *d_ptr;

	/* Card is in hand (instead of on table) */
	int hand;

	/* Card is eligible for being chosen */
	int eligible;

	/* Card should be seperated from others */
	int gapped;

	/* Card is selected */
	int selected;

	/* Card should be highlighted when selected */
	int highlight;

	/* Card is not eligible for selection, but should be colored anyway */
	int color;

	/* Card is covered (by a good) */
	int covered;

	/* Order card was played in (if on table) */
	int order;

} displayed;

/*
 * List of cards in hand.
 */
static displayed hand[MAX_DECK];
static int hand_size;

/*
 * List of cards on table (per player).
 */
static displayed table[MAX_PLAYER][MAX_DECK];
static int table_size[MAX_PLAYER];

/*
 * Cached status information to be displayed per player.
 */
typedef struct status_display
{
	/* Name to display */
	char name[80];

	/* Actions */
	int action[2];

	/* Victory point chips */
	int vp;

	/* Total victory points */
	int end_vp;

	/* Cards in hand */
	int cards_hand;

	/* Prestige */
	int prestige;

	/* Military strength */
	int military;

	/* Text of military strength tooltip */
	char military_tip[1024];

	/* Array of goals to display */
	int goal_display[MAX_GOAL];

	/* Array of goals to be grayed out */
	int goal_gray[MAX_GOAL];

	/* Array of goal tooltips */
	char goal_tip[MAX_GOAL][1024];

} status_display;

/*
 * Array of displayed status information per player.
 */
static status_display status_player[MAX_PLAYER];

/*
 * Other displayed status information.
 */
static int display_deck, display_discard, display_pool;


/*
 * Restriction types on action button sensitivity.
 */
#define RESTRICT_NUM      1
#define RESTRICT_BOTH     2
#define RESTRICT_PAY      3
#define RESTRICT_GOOD     4
#define RESTRICT_TAKEOVER 5
#define RESTRICT_DEFEND   6
#define RESTRICT_UPGRADE  7
#define RESTRICT_CONSUME  8
#define RESTRICT_START    9

/*
 * Restriction on action button.
 */
static int action_restrict;
static int action_min, action_max, action_payment_which, action_payment_mil;
static int action_cidx, action_oidx;

/*
 * Number of icon images.
 */
#define MAX_ICON 18

/*
 * Special icon numbers.
 */
#define ICON_NO_ACT    10
#define ICON_HANDSIZE  11
#define ICON_VP        12
#define ICON_MILITARY  13
#define ICON_PRESTIGE  14
#define ICON_WAITING   15
#define ICON_READY     16
#define ICON_OPTION    17

/*
 * Card images.
 */
static GdkPixbuf *image_cache[MAX_DESIGN];

/*
 * Goal card images.
 */
static GdkPixbuf *goal_cache[MAX_GOAL];

/*
 * Icon images.
 */
static GdkPixbuf *icon_cache[MAX_ICON];

/*
 * Action card images.
 */
static GdkPixbuf *action_cache[MAX_ACTION];

/*
 * Card back image.
 */
static GdkPixbuf *card_back;

/*
 * Widgets used in multiple functions.
 */
static GtkWidget *full_image;
static GtkWidget *hand_area;
static GtkWidget *player_area[MAX_PLAYER], *orig_area[MAX_PLAYER];
static GtkWidget *player_status[MAX_PLAYER], *orig_status[MAX_PLAYER];
static GtkWidget *player_box[MAX_PLAYER], *player_sep[MAX_PLAYER];
static GtkWidget *goal_area;
static GtkWidget *game_status;
static GtkWidget *main_hbox, *lobby_vbox;
static GtkWidget *phase_labels[MAX_ACTION];
static GtkWidget *action_box;
static GtkWidget *undo_item, *save_item;
static GtkWidget *entry_hbox;

/*
 * Lists for online functions.
 */
GtkListStore *user_list;
GtkTreeStore *game_list;

/*
 * Widgets used by network functions.
 */
GtkWidget *entry_label, *chat_view;
GtkWidget *games_view, *password_entry;
GtkWidget *create_button, *join_button, *leave_button, *kick_button;
GtkWidget *start_button;
GtkWidget *action_prompt, *action_button;

/*
 * Keyboard accelerator group for main window.
 */
static GtkAccelGroup *window_accel;

/*
 * Text buffer for message area.
 */
static GtkWidget *message_view;

/*
 * Mark at end of message area text buffer.
 */
static GtkTextMark *message_end;

/*
 * y-coordinate of line of "last seen" text in buffer.
 */
static int message_last_y;


/*
 * Add text to the message buffer.
 */
void message_add(game *g, char *msg)
{
	GtkTextIter end_iter;
	GtkTextBuffer *message_buffer;

	/* Get message buffer */
	message_buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(message_view));

	/* Get end mark */
	gtk_text_buffer_get_iter_at_mark(message_buffer, &end_iter,
	                                 message_end);

	/* Add message */
	gtk_text_buffer_insert(message_buffer, &end_iter, msg, -1);

	/* Scroll to end mark */
	gtk_text_view_scroll_mark_onscreen(GTK_TEXT_VIEW(message_view),
	                                   message_end);
}

/*
 * No need to ever wait for an answer.
 */
void wait_for_answer(game *g, int who)
{
}

/*
 * No need to prepare a phase in advance.
 */
void prepare_phase(game *g, int who, int phase, int arg)
{
}

/*
 * Use simple random number generator.
 */
int game_rand(game *g)
{
	/* Call simple random number generator */
	return simple_rand(&g->random_seed);
}

/*
 * Clear message log.
 */
static void clear_log(void)
{
	GtkTextBuffer *message_buffer;

	/* Get message buffer */
	message_buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(message_view));

	/* Clear text buffer */
	gtk_text_buffer_set_text(message_buffer, "", 0);

	/* Reset last seen line */
	message_last_y = 0;
}

/*
 * Draw a line across the message text view.
 */
static gboolean message_view_expose(GtkWidget *text_view, GdkEventExpose *event,
                                    gpointer data)
{
	int x, y;
	int w;

	/* Convert buffer coordinates to window coordinates */
	gtk_text_view_buffer_to_window_coords(GTK_TEXT_VIEW(text_view),
	                                      GTK_TEXT_WINDOW_WIDGET,
	                                      0, message_last_y, &x, &y);

	/* Don't draw line at very top of window */
	if (!y) return FALSE;

	/* Get widget width */
	w = text_view->allocation.width;

	/* Draw line across window */
	gdk_draw_line(event->window, text_view->style->black_gc, 0, y, w, y);

	/* Continue handling event */
	return FALSE;
}

/*
 * Add a separator at the end of all previously seen text.
 */
static void reset_text_separator(void)
{
	GtkTextIter end_iter;
	GtkTextBuffer *message_buffer;
	GdkRectangle rect;
	int x, y;

	/* Convert current line coordinates to window coordinates */
	gtk_text_view_buffer_to_window_coords(GTK_TEXT_VIEW(message_view),
	                                      GTK_TEXT_WINDOW_WIDGET,
	                                      0, message_last_y, &x, &y);

	/* Invalidate old line */
	gtk_widget_queue_draw_area(message_view, 0, y,
	                           message_view->allocation.width, 1);

	/* Get message buffer */
	message_buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(message_view));

	/* Get end mark */
	gtk_text_buffer_get_iter_at_mark(message_buffer, &end_iter,
	                                 message_end);

	/* Get location (in buffer coordinates) of end iterator */
	gtk_text_view_get_iter_location(GTK_TEXT_VIEW(message_view), &end_iter,
	                                &rect);

	/* Remember y-coordinate */
	message_last_y = rect.y;
}

/*
 * Load pixbufs with card images from image bundle.
 *
 * The image bundle format is nothing special -- it exists mainly to make
 * it difficult for people to get at the images directly.
 *
 * This was requested by Tom Lehmann.
 */
static void load_image_bundle(void)
{
	GFile *bundle;
	GInputStream *fs, *ms;
	GdkPixbuf **pix_ptr;
	char buf[1024], *data_buf;
	int count, x;

	/* Create bundle file handle */
	bundle = g_file_new_for_path(RFTGDIR "/images.data");

	/* Open file for reading */
	fs = G_INPUT_STREAM(g_file_read(bundle, NULL, NULL));

	/* Check for error */
	if (!fs)
	{
		/* Try reading from current directory instead */
		bundle = g_file_new_for_path("images.data");

		/* Open file for reading */
		fs = G_INPUT_STREAM(g_file_read(bundle, NULL, NULL));
	}

	/* Check for error */
	if (!fs)
	{
		/* Error */
		printf("Can't open raw images or image bundle!\n");
		return;
	}

	/* Read header */
	count = g_input_stream_read(fs, buf, 4, NULL, NULL);

	/* Check header */
	if (strncmp(buf, "RFTG", 4))
	{
		/* Error */
		printf("Image bundle missing header!\n");
		return;
	}

	/* Loop until end of file */
	while (1)
	{
		/* Get next type of image */
		count = g_input_stream_read(fs, buf, 1, NULL, NULL);

		/* Check for end of file */
		if (buf[0] == 0) break;

		/* Check for card image */
		if (buf[0] == 1)
		{
			/* Read card number */
			count = g_input_stream_read(fs, buf, 4, NULL, NULL);

			/* Convert to integer */
			x = strtol(buf, NULL, 10);

			/* Get pointer to pixbuf holder */
			pix_ptr = &image_cache[x];
		}

		/* Check for card back image */
		else if (buf[0] == 2)
		{
			/* Get pointer to pixbuf */
			pix_ptr = &card_back;
		}

		/* Check for goal image */
		else if (buf[0] == 3)
		{
			/* Read card number */
			count = g_input_stream_read(fs, buf, 3, NULL, NULL);

			/* Convert to integer */
			x = strtol(buf, NULL, 10);

			/* Get pointer to pixbuf holder */
			pix_ptr = &goal_cache[x];
		}

		/* Check for icon image */
		else if (buf[0] == 4)
		{
			/* Read card number */
			count = g_input_stream_read(fs, buf, 3, NULL, NULL);

			/* Convert to integer */
			x = strtol(buf, NULL, 10);

			/* Get pointer to pixbuf holder */
			pix_ptr = &icon_cache[x];
		}

		/* Check for action card image */
		else if (buf[0] == 5)
		{
			/* Read card number */
			count = g_input_stream_read(fs, buf, 2, NULL, NULL);

			/* Convert to integer */
			x = strtol(buf, NULL, 10);

			/* Get pointer to pixbuf holder */
			pix_ptr = &action_cache[x];
		}

		/* Check for something else */
		else
		{
			/* Error */
			printf("Bad image type!\n");
			break;
		}

		/* Read file size */
		count = g_input_stream_read(fs, buf, 8, NULL, NULL);

		/* Convert to integer */
		x = strtol(buf, NULL, 10);

		/* Create buffer for image data */
		data_buf = (char *)malloc(x);

		/* Read into buffer */
		count = g_input_stream_read(fs, data_buf, x, NULL, NULL);

		/* Check for not enough read */
		if (count < x)
		{
			/* Error */
			printf("Did not read enough image data!\n");
			break;
		}

		/* Create memory stream from image data */
		ms = g_memory_input_stream_new_from_data(data_buf, x, NULL);

		/* Read image from file stream */
		*pix_ptr = gdk_pixbuf_new_from_stream(ms, NULL, NULL);

		/* Close memory stream */
		g_input_stream_close(ms, NULL, NULL);

		/* Free memory */
		free(data_buf);

		/* Check for error */
		if (!(*pix_ptr))
		{
			/* Print error */
			printf("Error reading image from bundle!\n");
			break;
		}
	}

	/* Close stream */
	g_input_stream_close(fs, NULL, NULL);
}

/*
 * Load pixbufs with card images.
 */
static void load_images(void)
{
	int i;
	char fn[1024];

	/* Load card back image */
	card_back = gdk_pixbuf_new_from_file("image/cardback.jpg",NULL);

	/* Check for failure */
	if (!card_back)
	{
		/* Try to load image data from bundle instead */
		load_image_bundle();
		return;
	}

	/* Loop over designs */
	for (i = 0; i < MAX_DESIGN; i++)
	{
		/* Construct image filename */
		sprintf(fn, "image/card%03d.jpg", i);

		/* Load image */
		image_cache[i] = gdk_pixbuf_new_from_file(fn, NULL);

		/* Check for error */
		if (!image_cache[i])
		{
			/* Print error */
			printf("Cannot open card image %s!\n", fn);
		}
	}

	/* Loop over goals */
	for (i = 0; i < MAX_GOAL; i++)
	{
		/* Construct image filename */
		sprintf(fn, "image/goal%02d.jpg", i);

		/* Load image */
		goal_cache[i] = gdk_pixbuf_new_from_file(fn, NULL);

		/* Check for error */
		if (!goal_cache[i])
		{
			/* Print error */
			printf("Cannot open goal image %s!\n", fn);
		}
	}

	/* Loop over icons */
	for (i = 0; i < MAX_ICON; i++)
	{
		/* Skip second develop/settle action */
		if (i == ACT_DEVELOP2 || i == ACT_SETTLE2) continue;

		/* Construct image filename */
		sprintf(fn, "image/icon%02d.png", i);

		/* Load image */
		icon_cache[i] = gdk_pixbuf_new_from_file(fn, NULL);

		/* Check for error */
		if (!icon_cache[i])
		{
			/* Print error */
			printf("Cannot open icon image %s!\n", fn);
		}
	}

	/* Loop over actions */
	for (i = 0; i < MAX_ACTION; i++)
	{
		/* Skip second develop/settle action */
		if (i == ACT_DEVELOP2 || i == ACT_SETTLE2) continue;

		/* Construct image filename */
		sprintf(fn, "image/action%01d.jpg", i);

		/* Load image */
		action_cache[i] = gdk_pixbuf_new_from_file(fn, NULL);

		/* Check for error */
		if (!action_cache[i])
		{
			/* Print error */
			printf("Cannot open action card image %s!\n", fn);
		}
	}
}

/*
 * Function to determine whether enough cards are selected.
 */
static gboolean action_check_number(void)
{
	displayed *i_ptr;
	int i, n = 0;

	/* Loop over cards in hand */
	for (i = 0; i < hand_size; i++)
	{
		/* Get displayed card pointer */
		i_ptr = &hand[i];

		/* Skip unselected */
		if (!i_ptr->selected) continue;

		/* Count selected cards */
		n++;
	}

	/* Loop over cards on table */
	for (i = 0; i < table_size[player_us]; i++)
	{
		/* Get displayed card pointer */
		i_ptr = &table[player_us][i];

		/* Skip unselected */
		if (!i_ptr->selected) continue;

		/* Count selected cards */
		n++;
	}

	/* Check for not enough */
	if (n < action_min) return FALSE;

	/* Check for too many */
	if (n > action_max) return FALSE;

	/* Just right */
	return TRUE;
}

/*
 * Function to determine whether enough cards in hand and on table
 * are selected.
 */
static gboolean action_check_both(void)
{
	displayed *i_ptr;
	int i, n = 0, ns = 0;

	/* Loop over cards in hand */
	for (i = 0; i < hand_size; i++)
	{
		/* Get displayed card pointer */
		i_ptr = &hand[i];

		/* Skip unselected */
		if (!i_ptr->selected) continue;

		/* Count selected cards */
		n++;
	}

	/* Loop over cards on table */
	for (i = 0; i < table_size[player_us]; i++)
	{
		/* Get displayed card pointer */
		i_ptr = &table[player_us][i];

		/* Skip unselected */
		if (!i_ptr->selected) continue;

		/* Count selected cards */
		ns++;
	}

	/* Check for not enough */
	if (n < action_min) return FALSE;
	if (ns < action_min) return FALSE;

	/* Check for too many */
	if (n > action_max) return FALSE;
	if (ns > action_max) return FALSE;

	/* Check for hand selected but not table */
	if (n && !ns) return FALSE;

	/* Just right */
	return TRUE;
}

/*
 * Function to determine whether selected cards meet payment.
 */
static gboolean action_check_payment(void)
{
	game sim;
	displayed *i_ptr;
	int i, n = 0, ns = 0;
	int list[MAX_DECK], special[MAX_DECK];

	/* Loop over cards in hand */
	for (i = 0; i < hand_size; i++)
	{
		/* Get hand pointer */
		i_ptr = &hand[i];

		/* Skip unselected */
		if (!i_ptr->selected) continue;

		/* Add to regular list */
		list[n++] = i_ptr->index;
	}

	/* Loop over cards on table */
	for (i = 0; i < table_size[player_us]; i++)
	{
		/* Get table card pointer */
		i_ptr = &table[player_us][i];

		/* Skip unselected */
		if (!i_ptr->selected) continue;

		/* Add to special list */
		special[ns++] = i_ptr->index;
	}

	/* Copy game */
	sim = real_game;

	/* Set simulation flag */
	sim.simulation = 1;

	/* Try to make payment */
	return payment_callback(&sim, player_us, action_payment_which,
	                        list, n, special, ns, action_payment_mil);
}

/*
 * Function to determine whether selected goods can be consumed.
 */
static gboolean action_check_goods(void)
{
	game sim;
	displayed *i_ptr;
	int i, n = 0;
	int list[MAX_DECK];

	/* Loop over cards on table */
	for (i = 0; i < table_size[player_us]; i++)
	{
		/* Get displayed card pointer */
		i_ptr = &table[player_us][i];

		/* Skip unselected */
		if (!i_ptr->selected) continue;

		/* Add to regular list */
		list[n++] = i_ptr->index;
	}

	/* Check for too few */
	if (n < action_min) return 0;

	/* Copy game */
	sim = real_game;

	/* Set simulation flag */
	sim.simulation = 1;

	/* Try to make payment */
	return good_chosen(&sim, player_us, action_cidx, action_oidx, list, n);
}

/*
 * Function to determine whether selected card can be taken over.
 */
static gboolean action_check_takeover(void)
{
	game sim;
	displayed *i_ptr;
	int i, j;
	int target = -1, special = -1;

	/* Loop over opponents */
	for (i = 0; i < real_game.num_players; i++)
	{
		/* Skip ourself */
		if (i == player_us) continue;

		/* Loop over player's table area */
		for (j = 0; j < table_size[i]; j++)
		{
			/* Get displayed card pointer */
			i_ptr = &table[i][j];

			/* Skip unselected */
			if (!i_ptr->selected) continue;

			/* Check for too many targets */
			if (target != -1) return 0;
			
			/* Remember target world */
			target = i_ptr->index;
		}
	}

	/* Loop over cards on table */
	for (i = 0; i < table_size[player_us]; i++)
	{
		/* Get table card pointer */
		i_ptr = &table[player_us][i];

		/* Skip unselected */
		if (!i_ptr->selected) continue;

		/* Check for too many special cards */
		if (special != -1) return 0;

		/* Remember special card used */
		special = i_ptr->index;
	}

	/* Check for no target or special card */
	if (target == -1 && special == -1) return 1;

	/* Check for only no target */
	if (target == -1) return 0;

	/* Check for only no special card */
	if (special == -1) return 0;

	/* Copy game */
	sim = real_game;

	/* Set simulation flag */
	sim.simulation = 1;

	/* Check takeover legality */
	return takeover_callback(&sim, special, target);
}

/*
 * Function to determine whether selected cards are a legal defense.
 */
static gboolean action_check_defend(void)
{
	game sim;
	displayed *i_ptr;
	int i, n = 0, ns = 0;
	int list[MAX_DECK], special[MAX_DECK];

	/* Loop over cards in hand */
	for (i = 0; i < hand_size; i++)
	{
		/* Get hand pointer */
		i_ptr = &hand[i];

		/* Skip unselected */
		if (!i_ptr->selected) continue;

		/* Add to regular list */
		list[n++] = i_ptr->index;
	}

	/* Loop over cards on table */
	for (i = 0; i < table_size[player_us]; i++)
	{
		/* Get table card pointer */
		i_ptr = &table[player_us][i];

		/* Skip unselected */
		if (!i_ptr->selected) continue;

		/* Add to special list */
		special[ns++] = i_ptr->index;
	}

	/* Copy game */
	sim = real_game;

	/* Set simulation flag */
	sim.simulation = 1;

	/* Try to defend (we don't care about win/lose, just legality */
	return defend_callback(&sim, player_us, 0, list, n, special, ns);
}

/*
 * Function to determine whether selected cards are a legal world upgrade.
 */
static gboolean action_check_upgrade(void)
{
	game sim;
	displayed *i_ptr;
	int i, n = 0, ns = 0;
	int list[MAX_DECK], special[MAX_DECK];

	/* Loop over cards in hand */
	for (i = 0; i < hand_size; i++)
	{
		/* Get hand pointer */
		i_ptr = &hand[i];

		/* Skip unselected */
		if (!i_ptr->selected) continue;

		/* Add to regular list */
		list[n++] = i_ptr->index;
	}

	/* Loop over cards on table */
	for (i = 0; i < table_size[player_us]; i++)
	{
		/* Get table card pointer */
		i_ptr = &table[player_us][i];

		/* Skip unselected */
		if (!i_ptr->selected) continue;

		/* Add to special list */
		special[ns++] = i_ptr->index;
	}

	/* Check for no cards selected */
	if (!n && !ns) return 1;

	/* Check for more than one world or replacement selected */
	if (n > 1 || ns > 1) return 0;

	/* Check for only one of world or replacement selected */
	if (!n || !ns) return 0;

	/* Copy game */
	sim = real_game;

	/* Set simulation flag */
	sim.simulation = 1;

	/* Try to upgrade */
	return upgrade_chosen(&sim, player_us, list[0], special[0]);
}

/*
 * Function to determine whether selected cards are legal to consume.
 */
static gboolean action_check_consume(void)
{
	game sim;
	displayed *i_ptr;
	int i, n = 0;
	int list[MAX_DECK];

	/* Loop over cards in hand */
	for (i = 0; i < hand_size; i++)
	{
		/* Get hand pointer */
		i_ptr = &hand[i];

		/* Skip unselected */
		if (!i_ptr->selected) continue;

		/* Add to regular list */
		list[n++] = i_ptr->index;
	}

	/* Copy game */
	sim = real_game;

	/* Set simulation flag */
	sim.simulation = 1;

	/* Try to consume */
	return consume_hand_chosen(&sim, player_us, action_cidx, action_oidx,
	                           list, n);
}

/*
 * Return whether the selected world and hand is a valid start.
 */
static int action_check_start(void)
{
	game sim;
	displayed *i_ptr;
	int i, n = 0, ns = 0;
	int list[MAX_DECK], special[MAX_DECK];

	/* Loop over cards in hand */
	for (i = 0; i < hand_size; i++)
	{
		/* Get hand pointer */
		i_ptr = &hand[i];

		/* Skip unselected */
		if (!i_ptr->selected) continue;

		/* Add to regular list */
		list[n++] = i_ptr->index;
	}

	/* Loop over cards on table */
	for (i = 0; i < table_size[player_us]; i++)
	{
		/* Get table card pointer */
		i_ptr = &table[player_us][i];

		/* Skip unselected */
		if (!i_ptr->selected) continue;

		/* Add to special list */
		special[ns++] = i_ptr->index;
	}

	/* Check for exactly 1 world selected */
	if (ns != 1) return 0;

	/* Copy game */
	sim = real_game;

	/* Set simulation flag */
	sim.simulation = 1;

	/* Try to start */
	return start_callback(&sim, player_us, list, n, special, ns);
}

/*
 * Refresh the full-size card image.
 *
 * Called when the pointer moves over a small card image.
 */
static gboolean redraw_full(GtkWidget *widget, GdkEventCrossing *event,
                            gpointer data)
{
	design *d_ptr = (design *)data;
	GdkPixbuf *buf;

	/* Check for no design */
	if (!d_ptr)
	{
		/* Set image to card back */
		buf = card_back;
	}
	else
	{
		/* Set image to card face */
		buf = image_cache[d_ptr->index];
	}

	/* Check for halfsize image */
	if (opt.full_reduced == 1)
	{
		/* Scale image */
		buf = gdk_pixbuf_scale_simple(buf, CARD_WIDTH / 2,
		                          CARD_HEIGHT / 2, GDK_INTERP_BILINEAR);
	}

	/* Set image */
	gtk_image_set_from_pixbuf(GTK_IMAGE(full_image), buf);

	/* Check for halfsize image */
	if (opt.full_reduced == 1)
	{
		/* Remove our scaled buffer */
		g_object_unref(G_OBJECT(buf));
	}

	/* Event handled */
	return TRUE;
}

/*
 * Refresh the full-size card image with an action card.
 *
 * Called when the pointer moves over a action button.
 */
static gboolean redraw_action(GtkWidget *widget, GdkEventCrossing *event,
                              gpointer data)
{
	int a = GPOINTER_TO_INT(data);
	GdkPixbuf *buf;

	/* Set image to card face */
	buf = action_cache[a];

	/* Check for halfsize image */
	if (opt.full_reduced == 1)
	{
		/* Scale image */
		buf = gdk_pixbuf_scale_simple(buf, CARD_WIDTH / 2,
		                          CARD_HEIGHT / 2, GDK_INTERP_BILINEAR);
	}

	/* Set image */
	gtk_image_set_from_pixbuf(GTK_IMAGE(full_image), buf);

	/* Check for halfsize image */
	if (opt.full_reduced == 1)
	{
		/* Remove our scaled buffer */
		g_object_unref(G_OBJECT(buf));
	}

	/* Continue to handle event */
	return FALSE;
}

/*
 * Create an event box containing the given card's image.
 */
static GtkWidget *new_image_box(design *d_ptr, int w, int h, int color,
                                int border, int back)
{
	GdkPixbuf *buf, *border_buf, *blank_buf;
	GtkWidget *image, *box;
	int bw;

	/* Check for no image */
	if (back)
	{
		/* Scale card back image */
		buf = gdk_pixbuf_scale_simple(card_back, w, h,
		                              GDK_INTERP_BILINEAR);
	}
	else
	{
		/* Scale image */
		buf = gdk_pixbuf_scale_simple(image_cache[d_ptr->index], w, h,
		                              GDK_INTERP_BILINEAR);
	}

	/* Check for grayscale */
	if (!color)
	{
		/* Desaturate */
		gdk_pixbuf_saturate_and_pixelate(buf, buf, 0, TRUE);
	}

	/* Check for border placed around image */
	if (border)
	{
		/* Compute border width */
		bw = w / 20;

		/* Enforce minimum border width */
		if (bw < 5) bw = 5;

		/* Create a border pixbuf */
		border_buf = gdk_pixbuf_new(GDK_COLORSPACE_RGB, TRUE, 8, w, h);

		/* Fill pixbuf with opaque yellow */
		gdk_pixbuf_fill(border_buf, 0xffff00ff);

		/* Create a blank pixbuf */
		blank_buf = gdk_pixbuf_new(GDK_COLORSPACE_RGB, TRUE, 8, w, h);

		/* Fill pixbuf with transparent black */
		gdk_pixbuf_fill(blank_buf, 0);

		/* Copy blank space onto middle of border buffer */
		gdk_pixbuf_copy_area(blank_buf, bw, bw, w - 2 * bw, h - 2 * bw,
		                     border_buf, bw, bw); 

		/* Composite border onto card image buffer */
		gdk_pixbuf_composite(border_buf, buf, 0, 0, w, h, 0, 0, 1, 1,
		                     GDK_INTERP_BILINEAR, 255);

		/* Release our copies of pixbufs */
		g_object_unref(G_OBJECT(blank_buf));
		g_object_unref(G_OBJECT(border_buf));
	}

	/* Make image widget */
	image = gtk_image_new_from_pixbuf(buf);

	/* Destroy our copy of the pixbuf */
	g_object_unref(G_OBJECT(buf));

	/* Make event box for image */
	box = gtk_event_box_new();

	/* Add image to event box */
	gtk_container_add(GTK_CONTAINER(box), image);

	/* Connect "pointer enter" signal */
	g_signal_connect(G_OBJECT(box), "enter-notify-event",
	                 G_CALLBACK(redraw_full), d_ptr);

	/* Return event box widget */
	return box;
}

/*
 * Card selected/deselected.
 */
static gboolean card_selected(GtkWidget *widget, GdkEventButton *event,
                              gpointer data)
{
	displayed *i_ptr = (displayed *)data;

	/* Change selection status */
	i_ptr->selected = !i_ptr->selected;

	/* Check for "number" restriction on action button */
	if (action_restrict == RESTRICT_NUM)
	{
		/* Set sensitivity */
		gtk_widget_set_sensitive(action_button, action_check_number());
	}

	/* Check for "both hand and active" restriction on action button */
	else if (action_restrict == RESTRICT_BOTH)
	{
		/* Set sensitivity */
		gtk_widget_set_sensitive(action_button, action_check_both());
	}

	/* Check for "payment" restriction on action button */
	else if (action_restrict == RESTRICT_PAY)
	{
		/* Set sensitivity */
		gtk_widget_set_sensitive(action_button, action_check_payment());
	}

	/* Check for "goods" restriction on action button */
	else if (action_restrict == RESTRICT_GOOD)
	{
		/* Set sensitivity */
		gtk_widget_set_sensitive(action_button, action_check_goods());
	}

	/* Check for "takeover" restriction on action button */
	else if (action_restrict == RESTRICT_TAKEOVER)
	{
		/* Set sensitivity */
		gtk_widget_set_sensitive(action_button,action_check_takeover());
	}

	/* Check for "defend" restriction on action button */
	else if (action_restrict == RESTRICT_DEFEND)
	{
		/* Set sensitivity */
		gtk_widget_set_sensitive(action_button, action_check_defend());
	}

	/* Check for "upgrade" restriction on action button */
	else if (action_restrict == RESTRICT_UPGRADE)
	{
		/* Set sensitivity */
		gtk_widget_set_sensitive(action_button, action_check_upgrade());
	}

	/* Check for "consume" restriction on action button */
	else if (action_restrict == RESTRICT_CONSUME)
	{
		/* Set sensitivity */
		gtk_widget_set_sensitive(action_button, action_check_consume());
	}

	/* Check for "start world" restriction on action button */
	else if (action_restrict == RESTRICT_START)
	{
		/* Set sensitivity */
		gtk_widget_set_sensitive(action_button, action_check_start());
	}

	/* Check for card in hand */
	if (i_ptr->hand)
	{
		/* Redraw hand */
		redraw_hand();
	}
	else
	{
		/* Redraw table */
		redraw_table();
	}

	/* Event handled */
	return TRUE;
}

/*
 * Card selected by keypress.
 */
static void card_keyed(GtkWidget *widget, gpointer data)
{
	/* Call regular handler */
	card_selected(widget, NULL, data);
}

/*
 * Callback to destroy child widgets so that new ones can take their place.
 */
static void destroy_widget(GtkWidget *widget, gpointer data)
{
	/* Destroy widget */
	gtk_widget_destroy(widget);
}

/*
 * Redraw hand area.
 */
void redraw_hand(void)
{
	GtkWidget *box;
	displayed *i_ptr;
	int count = 0, gap = 1, n, num_gap = 0;
	int key_count = 1, key_code;
	int width, height;
	int card_w, card_h;
	int i;

	/* First destroy all pre-existing card widgets */
	gtk_container_foreach(GTK_CONTAINER(hand_area), destroy_widget, NULL);

	/* Get number of cards in hand */
	n = hand_size;

	/* Loop over cards in hand */
	for (i = 0; i < n; i++)
	{
		/* Get card pointer */
		i_ptr = &hand[i];

		/* Check for "gapped" card */
		if (i_ptr->gapped)
		{
			/* Count cards marked for gap */
			num_gap++;
		}
	}

	/* Check for some but not all cards needing gap */
	if (num_gap > 0 && num_gap < n)
	{
		/* Add extra space for gap */
		n++;
	}
	else
	{
		/* Mark gap as not needed */
		gap = 0;
	}

	/* Get hand area width and height */
	width = hand_area->allocation.width;
	height = hand_area->allocation.height;

	/* Get width of individual card */
	card_w = width / 6;

	/* Compute height of card */
	card_h = card_w * CARD_HEIGHT / CARD_WIDTH;

	/* Compute pixels per card */
	if (n > 0) width = width / n;

	/* Maximum width */
	if (width > card_w) width = card_w;

	/* Loop over cards */
	for (i = 0; i < hand_size; i++)
	{
		/* Get card pointer */
		i_ptr = &hand[i];

		/* Skip spot before first gap card */
		if (i_ptr->gapped && gap)
		{
			/* Increase count */
			count++;

			/* Gap no longer needed */
			gap = 0;
		}

		/* Get event box with image */
		box = new_image_box(i_ptr->d_ptr, card_w, card_h,
		                    i_ptr->eligible || i_ptr->color,
		                    i_ptr->highlight && i_ptr->selected, 0);

		/* Place event box */
		gtk_fixed_put(GTK_FIXED(hand_area), box, count * width,
		              i_ptr->selected && !i_ptr->highlight ? 0 :
		                                             height - card_h);

		/* Check for eligible card */
		if (i_ptr->eligible)
		{
			/* Connect "button released" signal */
			g_signal_connect(G_OBJECT(box), "button-release-event",
			                 G_CALLBACK(card_selected), i_ptr);

			/* Check for enough keyboard numbers */
			if (key_count < 10)
			{
				/* Compute keyboard code */
				if (key_count <= 9)
				{
					/* Start with '1' */
					key_code = GDK_1 + key_count - 1;
				}
				else
				{
					/* Use '0' for 10 */
					key_code = GDK_0;
				}

				/* Add handler for keypresses */
				gtk_widget_add_accelerator(box,
				                           "key-signal",
							   window_accel,
							   key_code,
							   0, 0);

				/* Connect key-signal */
				g_signal_connect(G_OBJECT(box), "key-signal",
						 G_CALLBACK(card_keyed),
				                 i_ptr);

				/* Increment count */
				key_count++;
			}
		}

		/* Show image */
		gtk_widget_show_all(box);

		/* Count images shown */
		count++;
	}
}

/*
 * Redraw a player's table area.
 */
static void redraw_table_area(int who, GtkWidget *area)
{
	GtkWidget *box, *good_box;
	displayed *i_ptr;
	int x = 0, y = 0;
	int col, row;
	int width, height;
	int card_w, card_h;
	int i, n;

	/* First destroy all pre-existing card widgets */
	gtk_container_foreach(GTK_CONTAINER(area), destroy_widget, NULL);

	/* Number of cards to display */
	n = table_size[who];

	/* Always have room for 12 cards */
	if (n < 12) n = 12;

	/* Get hand area width and height */
	width = area->allocation.width;
	height = area->allocation.height;

	/* Check for wide area */
	if (width > height)
	{
		/* Six columns */
		col = 6;
	}

	/* Check for narrow area */
	else if (height > (3 * width) / 2)
	{
		/* Three columns */
		col = 3;
	}

	else
	{
		/* Four columns */
		col = 4;
	}

	/* Compute number of rows needed */
	row = (n + col - 1) / col;

	/* Get width of individual card */
	card_w = width / col;

	/* Compute height of card */
	card_h = card_w * CARD_HEIGHT / CARD_WIDTH;

	/* Height of row */
	height = height / row;

	/* Width is card width */
	width = card_w;

	/* Loop over cards */
	for (i = 0; i < table_size[who]; i++)
	{
		/* Get displayed card pointer */
		i_ptr = &table[who][i];

		/* Get event box with image */
		box = new_image_box(i_ptr->d_ptr, card_w, card_h,
		                    i_ptr->eligible || i_ptr->color,
		                    i_ptr->highlight && i_ptr->selected, 0);

		/* Place event box */
		gtk_fixed_put(GTK_FIXED(area), box, x * width, y * height);

		/* Show image */
		gtk_widget_show_all(box);

		/* Check for good */
		if (i_ptr->covered || (i_ptr->selected && !i_ptr->highlight))
		{
			/* Get event box with no image */
			good_box = new_image_box(i_ptr->d_ptr, 3 * card_w / 4,
			                                       3 * card_h / 4,
			                               i_ptr->eligible ||
			                                 i_ptr->color, 0, 1);

			/* Place box on card */
			gtk_fixed_put(GTK_FIXED(area), good_box,
			              x * width + card_w / 4,
			              i_ptr->selected && !i_ptr->highlight ?
			                  y * height :
				          y * height + card_h / 4);

			/* Show image */
			gtk_widget_show_all(good_box);

			/* Check for eligible card */
			if (i_ptr->eligible)
			{
				/* Connect "button released" signal */
				g_signal_connect(G_OBJECT(good_box),
				                 "button-release-event",
						 G_CALLBACK(card_selected),
				                 i_ptr);
			}
		}

		/* Check for eligible card */
		if (i_ptr->eligible)
		{
			/* Connect "button released" signal */
			g_signal_connect(G_OBJECT(box), "button-release-event",
					 G_CALLBACK(card_selected), i_ptr);
		}

		/* Next slot */
		x++;

		/* Check for next row */
		if (x == col)
		{
			/* Go to next row */
			x = 0; y++;
		}
	}
}

/*
 * Redraw all player areas of the table.
 */
void redraw_table(void)
{
	int i;

	/* Loop over players */
	for (i = 0; i < real_game.num_players; i++)
	{
		/* Redraw player area */
		redraw_table_area(i, player_area[i]);
	}
}

/*
 * Create a tooltip for a goal image.
 */
static char *goal_tooltip(game *g, int goal)
{
	static char msg[1024];
	player *p_ptr;
	int i;
	char text[1024];

	/* Create tooltip text */
	sprintf(msg, "%s", goal_name[goal]);

	/* Check for first goal */
	if (goal <= GOAL_FIRST_4_MILITARY)
	{
		/* Check for claimed goal */
		if (!g->goal_avail[goal])
		{
			/* Add text to tooltip */
			strcat(msg, "\nClaimed by:");

			/* Loop over players */
			for (i = 0; i < g->num_players; i++)
			{
				/* Get player pointer */
				p_ptr = &g->p[i];

				/* Check for claim */
				if (p_ptr->goal_claimed[goal])
				{
					/* Add name to tooltip */
					strcat(msg, "\n  ");
					strcat(msg, p_ptr->name);
				}
			}
		}
	}

	/* Check for most goal */
	if (goal >= GOAL_MOST_MILITARY)
	{
		/* Add text to tooltip */
		strcat(msg, "\nProgress:");

		/* Loop over players */
		for (i = 0; i < g->num_players; i++)
		{
			/* Get player pointer */
			p_ptr = &g->p[i];

			/* Create progress string */
			sprintf(text, "\n%c %s: %d",
				p_ptr->goal_claimed[goal] ? '*' : ' ',
				p_ptr->name, p_ptr->goal_progress[goal]);

			/* Add progress string to tooltip */
			strcat(msg, text);
		}
	}

	/* Return tooltip text */
	return msg;
}

/*
 * Redraw the goal area.
 */
void redraw_goal(void)
{
	GtkWidget *image;
	GdkPixbuf *buf;
	int i, n;
	int width, height, goal_h, y = 0;

	/* First destroy all pre-existing goal widgets */
	gtk_container_foreach(GTK_CONTAINER(goal_area), destroy_widget, NULL);

	/* Assume six goals */
	n = 6;

	/* Get goal area width and height */
	width = goal_area->allocation.width;
	height = goal_area->allocation.height;

	/* Loop over goals */
	for (i = 0; i < MAX_GOAL; i++)
	{
		/* Skip inactive goals */
		if (!real_game.goal_active[i]) continue;

		/* Check for "first" goal */
		if (i <= GOAL_FIRST_4_MILITARY)
		{
			/* Compute height of "first" goal */
			goal_h = width * GOALF_HEIGHT / GOALF_WIDTH;
		}
		else
		{
			/* Compute height of "most" goal */
			goal_h = width * GOALM_HEIGHT / GOALM_WIDTH;
		}

		/* Create goal image */
		buf = gdk_pixbuf_scale_simple(goal_cache[i], width, goal_h,
		                              GDK_INTERP_BILINEAR);

		/* Check for unavailable goal */
		if (!real_game.goal_avail[i])
		{
			/* Desaturate */
			gdk_pixbuf_saturate_and_pixelate(buf, buf, 0, TRUE);
		}

		/* Make image widget */
		image = gtk_image_new_from_pixbuf(buf);

		/* Destroy local copy of the pixbuf */
		g_object_unref(G_OBJECT(buf));

		/* Place image */
		gtk_fixed_put(GTK_FIXED(goal_area), image, 0, y * height / 100);

		/* Add tooltip */
		gtk_widget_set_tooltip_text(image, goal_tooltip(&real_game, i));

		/* Show image */
		gtk_widget_show(image);

		/* Adjust distance to next card */
		if (i <= GOAL_FIRST_4_MILITARY)
		{
			/* Give "first" goals 15% */
			y += 15;
		}
		else
		{
			/* Give "most" goals 20% */
			y += 20;
		}
	}
}

/*
 * Extra text and font string to be drawn on an image.
 */
struct extra_info
{
	char text[1024];
	char *fontstr;
	int border;
};

/*
 * Set of "extra info" structures.
 */
static struct extra_info status_extra_info[MAX_PLAYER][4];

/*
 * Draw extra text on top of a GtkImage's window.
 */
static gboolean draw_extra_text(GtkWidget *image, GdkEventExpose *event,
                                gpointer data)
{
	GdkWindow *w;
	PangoLayout *layout;
	PangoFontDescription *font;
	int tw, th;
	int x, y;
	struct extra_info *ei = (struct extra_info *)data;

	/* Get window to draw on */
	w = gtk_widget_get_window(image);

	/* Create pango layout */
	layout = gtk_widget_create_pango_layout(image, NULL);

	/* Set marked-up text */
	pango_layout_set_markup(layout, ei->text, -1);

	/* Set alignment to center */
	pango_layout_set_alignment(layout, PANGO_ALIGN_CENTER);

	/* Create font description for text */
	font = pango_font_description_from_string(ei->fontstr);

	/* Set layout's font */
	pango_layout_set_font_description(layout, font);

	/* Get size of text */
	pango_layout_get_pixel_size(layout, &tw, &th);

	/* Compute point to start drawing */
	x = (image->allocation.width - tw) / 2 + image->allocation.x;
	y = (image->allocation.height - th) / 2 + image->allocation.y;

	/* Draw border around text if asked */
	if (ei->border)
	{
		gdk_draw_layout(w, image->style->white_gc, x - 1, y - 1,layout);
		gdk_draw_layout(w, image->style->white_gc, x + 1, y + 1,layout);
		gdk_draw_layout(w, image->style->white_gc, x + 1, y - 1,layout);
		gdk_draw_layout(w, image->style->white_gc, x - 1, y + 1,layout);
	}

	/* Draw layout on top of image */
	gdk_draw_layout(w, image->style->black_gc, x, y, layout);

	/* Free font description */
	pango_font_description_free(font);

	/* Continue handling event */
	return FALSE;
}

/*
 * Create a tooltip for a military strength icon.
 */
static char *get_military_tooltip(game *g, int who)
{
	static char msg[1024];
	char text[1024];
	card *c_ptr;
	power *o_ptr;
	int i, j;
	int base, rebel, blue, brown, green, yellow;

	/* Begin with base military strength */
	base = total_military(g, who);

	/* Create text */
	sprintf(msg, "Base strength: %+d", base);

	/* Start other strengths at base */
	rebel = blue = brown = green = yellow = base;

	/* Loop over cards */
	for (i = 0; i < g->deck_size; i++)
	{
		/* Get card pointer */
		c_ptr = &g->deck[i];

		/* Skip cards not belonging to current player */
		if (c_ptr->owner != who) continue;

		/* Skip inactive cards */
		if (c_ptr->where != WHERE_ACTIVE) continue;

		/* Skip just-played cards */
		if (c_ptr->temp) continue;

		/* Loop over card's powers */
		for (j = 0; j < c_ptr->d_ptr->num_power; j++)
		{
			/* Get power pointer */
			o_ptr = &c_ptr->d_ptr->powers[j];

			/* Skip incorrect phase */
			if (o_ptr->phase != PHASE_SETTLE) continue;

			/* Skip non-military powers */
			if (!(o_ptr->code & P3_EXTRA_MILITARY)) continue;

			/* Check for strength against rebels */
			if (o_ptr->code & P3_AGAINST_REBEL)
				rebel += o_ptr->value;

			/* Check for strength against Novelty worlds */
			if (o_ptr->code & P3_NOVELTY)
				blue += o_ptr->value;

			/* Check for strength against Rare worlds */
			if (o_ptr->code & P3_RARE)
				brown += o_ptr->value;

			/* Check for strength against Gene worlds */
			if (o_ptr->code & P3_GENE)
				green += o_ptr->value;

			/* Check for strength against Alien worlds */
			if (o_ptr->code & P3_ALIEN)
				yellow += o_ptr->value;
		}
	}

	/* Add rebel strength if different than base */
	if (rebel != base)
	{
		/* Create rebel text */
		sprintf(text, "\nAgainst Rebel: %+d", rebel);
		strcat(msg, text);
	}

	/* Add Novelty strength if different than base */
	if (blue != base)
	{
		/* Create text */
		sprintf(text, "\nAgainst Novelty: %+d", blue);
		strcat(msg, text);
	}

	/* Add Rare strength if different than base */
	if (brown != base)
	{
		/* Create text */
		sprintf(text, "\nAgainst Rare: %+d", brown);
		strcat(msg, text);
	}

	/* Add Gene strength if different than base */
	if (green != base)
	{
		/* Create text */
		sprintf(text, "\nAgainst Gene: %+d", green);
		strcat(msg, text);
	}

	/* Add Alien strength if different than base */
	if (yellow != base)
	{
		/* Create text */
		sprintf(text, "\nAgainst Alien: %+d", yellow);
		strcat(msg, text);
	}

	/* Check for active Imperium card */
	if (count_active_flags(g, who, FLAG_IMPERIUM))
	{
		/* Add vulnerability text */
		strcat(msg, "\nIMPERIUM card played");
	}

	/* Check for active Rebel military world */
	if (count_active_flags(g, who, FLAG_MILITARY | FLAG_REBEL))
	{
		/* Add vulnerability text */
		strcat(msg, "\nREBEL Military world played");
	}

	/* Return tooltip text */
	return msg;
}

/*
 * Create an image widget displaying an action icon.
 *
 * We superimpose the regular icon on the prestige icon for super actions.
 */
static GtkWidget *action_icon(int act, int size)
{
	GtkWidget *image;
	GdkPixbuf *buf, *iconbuf;
	int alpha;

	/* Check for second Develop */
	if ((act & ACT_MASK) == ACT_DEVELOP2)
		act = (act & ACT_PRESTIGE) | ACT_DEVELOP;

	/* Check for second Settle */
	if ((act & ACT_MASK) == ACT_SETTLE2)
		act = (act & ACT_PRESTIGE) | ACT_SETTLE;

	/* Check for prestige action */
	if (act & ACT_PRESTIGE)
	{
		/* Scale prestige icon */
		buf = gdk_pixbuf_scale_simple(icon_cache[ICON_PRESTIGE],
		                              size, size,
					      GDK_INTERP_BILINEAR);

		/* Make icon semi-transparent */
		alpha = 200;
	}
	else
	{
		/* Create empty pixbuf */
		buf = gdk_pixbuf_new(GDK_COLORSPACE_RGB, TRUE, 8, size, size);

		/* Fill pixbuf with transparent black */
		gdk_pixbuf_fill(buf, 0);

		/* Make icon fully opaque */
		alpha = 255;
	}

	/* Scale action icon down to correct size */
	iconbuf = gdk_pixbuf_scale_simple(icon_cache[act & ACT_MASK],
	                                  size, size, GDK_INTERP_BILINEAR);

	/* Composite action icon onto prestige/blank buffer */
	gdk_pixbuf_composite(iconbuf, buf, 0, 0, size, size,
	                     0, 0, 1, 1, GDK_INTERP_BILINEAR, alpha);

	/* Make image widget */
	image = gtk_image_new_from_pixbuf(buf);

	/* Destroy our copy of the icons */
	g_object_unref(G_OBJECT(buf));
	g_object_unref(G_OBJECT(iconbuf));

	/* Return image widget */
	return image;
}

/*
 * Redraw a player's status information.
 */
static void redraw_status_area(int who, GtkWidget *box)
{
	status_display *s_ptr;
	GtkWidget *image, *label;
	GdkPixbuf *buf;
	int width, height;
	int act0, act1;
	int i;
	struct extra_info *ei;

	/* Get information to display */
	s_ptr = &status_player[who];

	/* First destroy all pre-existing widgets */
	gtk_container_foreach(GTK_CONTAINER(box), destroy_widget, NULL);

	/* Create blank filler label */
	label = gtk_label_new("");

	/* Add filler label to box */
	gtk_box_pack_start(GTK_BOX(box), label, TRUE, TRUE, 0);

	/* Get status area height */
	height = box->allocation.height;

	/* Check for online game */
	if (client_state != CS_DISCONN)
	{
		/* Create label with player's name */
		label = gtk_label_new(s_ptr->name);

		/* Add name label to box */
		gtk_box_pack_start(GTK_BOX(box), label, FALSE, FALSE, 5);

		/* Check for waiting on player */
		switch (waiting_player[who])
		{
			/* Player is ready */
			case WAIT_READY:
				image = action_icon(ICON_READY, height);
				break;

			/* Waiting on player */
			case WAIT_BLOCKED:
				image = action_icon(ICON_WAITING, height);
				break;

			/* Player has option to play */
			case WAIT_OPTION:
				image = action_icon(ICON_OPTION, height);
				break;
		}

		/* Pack icon into status box */
		gtk_box_pack_start(GTK_BOX(box), image, FALSE, FALSE, 0);
	}

	/* Copy actions */
	act0 = s_ptr->action[0];
	act1 = s_ptr->action[1];

	/* Check for unselected actions */
	if (act0 == -1) act0 = ICON_NO_ACT;
	if (act1 == -1) act1 = ICON_NO_ACT;

	/* Check for non-advanced game */
	if (!real_game.advanced) act1 = -1;

	/* Make image widget */
	image = action_icon(act0, height);

	/* Pack icon into status box */
	gtk_box_pack_start(GTK_BOX(box), image, FALSE, FALSE, 0);

	/* Check for second action */
	if (act1 != -1)
	{
		/* Make image widget */
		image = action_icon(act1, height);

		/* Pack icon into status box */
		gtk_box_pack_start(GTK_BOX(box), image, FALSE, FALSE, 0);
	}

	/* Create handsize icon image */
	buf = gdk_pixbuf_scale_simple(icon_cache[ICON_HANDSIZE], height, height,
	                              GDK_INTERP_BILINEAR);

	/* Create image from pixbuf */
	image = gtk_image_new_from_pixbuf(buf);

	/* Pointer to extra info structure */
	ei = &status_extra_info[who][0];

	/* Create text for handsize */
	sprintf(ei->text, "<b>%d</b>", s_ptr->cards_hand);

	/* Set font */
	ei->fontstr = "Sans 12";

	/* Draw text a border */
	ei->border = 1;

	/* Connect expose-event to draw extra text */
	g_signal_connect_after(G_OBJECT(image), "expose-event",
	                       G_CALLBACK(draw_extra_text), ei);

	/* Destroy our copy of the icon */
	g_object_unref(G_OBJECT(buf));

	/* Pack icon into status box */
	gtk_box_pack_start(GTK_BOX(box), image, FALSE, FALSE, 0);

	/* Create victory point icon image */
	buf = gdk_pixbuf_scale_simple(icon_cache[ICON_VP], height, height,
	                              GDK_INTERP_BILINEAR);

	/* Create image from pixbuf */
	image = gtk_image_new_from_pixbuf(buf);

	/* Pointer to extra info structure */
	ei = &status_extra_info[who][1];

	/* Create text for victory points */
	sprintf(ei->text, "<b>%d\n%d</b>", s_ptr->vp, s_ptr->end_vp);

	/* Set font */
	ei->fontstr = "Sans 10";

	/* Draw text a border */
	ei->border = 1;

	/* Connect expose-event to draw extra text */
	g_signal_connect_after(G_OBJECT(image), "expose-event",
	                       G_CALLBACK(draw_extra_text), ei);

	/* Destroy our copy of the icon */
	g_object_unref(G_OBJECT(buf));

	/* Pack icon into status box */
	gtk_box_pack_start(GTK_BOX(box), image, FALSE, FALSE, 0);

	/* Check for third expansion */
	if (real_game.expanded >= 3)
	{
		/* Create prestige icon image */
		buf = gdk_pixbuf_scale_simple(icon_cache[ICON_PRESTIGE],
		                              height, height,
					      GDK_INTERP_BILINEAR);

		/* Create image from pixbuf */
		image = gtk_image_new_from_pixbuf(buf);

		/* Pointer to extra info structure */
		ei = &status_extra_info[who][2];

		/* Create text for prestige */
		sprintf(ei->text, "<b>%d</b>", s_ptr->prestige);

		/* Set font */
		ei->fontstr = "Sans 12";

		/* Draw text a border */
		ei->border = 1;

		/* Connect expose-event to draw extra text */
		g_signal_connect_after(G_OBJECT(image), "expose-event",
				       G_CALLBACK(draw_extra_text), ei);

		/* Destroy our copy of the icon */
		g_object_unref(G_OBJECT(buf));

		/* Pack icon into status box */
		gtk_box_pack_start(GTK_BOX(box), image, FALSE, FALSE, 0);
	}

	/* Create military icon image */
	buf = gdk_pixbuf_scale_simple(icon_cache[ICON_MILITARY], height, height,
	                              GDK_INTERP_BILINEAR);

	/* Create image from pixbuf */
	image = gtk_image_new_from_pixbuf(buf);

	/* Pointer to extra info structure */
	ei = &status_extra_info[who][3];

	/* Create text for military strength */
	sprintf(ei->text, "<span foreground=\"red\" weight=\"bold\">%+d</span>",
	        s_ptr->military);

	/* Set font */
	ei->fontstr = "Sans 10";

	/* No border */
	ei->border = 0;

	/* Connect expose-event to draw extra text */
	g_signal_connect_after(G_OBJECT(image), "expose-event",
	                       G_CALLBACK(draw_extra_text), ei);

	/* Destroy our copy of the icon */
	g_object_unref(G_OBJECT(buf));

	/* Add military strength tooltip */
	gtk_widget_set_tooltip_text(image, s_ptr->military_tip);

	/* Pack icon into status box */
	gtk_box_pack_start(GTK_BOX(box), image, FALSE, FALSE, 0);

	/* Loop over goals */
	for (i = 0; i < MAX_GOAL; i++)
	{
		/* Skip goals not to be displayed */
		if (!s_ptr->goal_display[i]) continue;

		/* Check for "first" goal */
		if (i <= GOAL_FIRST_4_MILITARY)
		{
			/* Compute width of "first" goal */
			width = height * GOALF_WIDTH / GOALF_HEIGHT;
		}
		else
		{
			/* Compute width of "most" goal */
			width = height * GOALM_WIDTH / GOALM_HEIGHT;
		}

		/* Create goal image */
		buf = gdk_pixbuf_scale_simple(goal_cache[i], width, height,
		                              GDK_INTERP_BILINEAR);

		/* Check for grayed goal */
		if (s_ptr->goal_gray[i])
		{
			/* Desaturate */
			gdk_pixbuf_saturate_and_pixelate(buf, buf, 0, TRUE);
		}

		/* Make image widget */
		image = gtk_image_new_from_pixbuf(buf);

		/* Destroy our copy of the icon */
		g_object_unref(G_OBJECT(buf));

		/* Add tooltip */
		gtk_widget_set_tooltip_text(image, s_ptr->goal_tip[i]);

		/* Pack icon into status box */
		gtk_box_pack_start(GTK_BOX(box), image, FALSE, FALSE, 0);
	}

	/* Create blank label */
	label = gtk_label_new("");

	/* Add label to box */
	gtk_box_pack_start(GTK_BOX(box), label, TRUE, TRUE, 0);

	/* Show everything */
	gtk_widget_show_all(box);
}

/*
 * Redraw all player's status information.
 */
void redraw_status(void)
{
	char buf[1024];
	int i;

	/* Loop over players */
	for (i = 0; i < real_game.num_players; i++)
	{
		/* Redraw player's status */
		redraw_status_area(i, player_status[i]);
	}

	/* Create status label */
	sprintf(buf, "Draw: %d  Discard: %d  Pool: %d", display_deck,
	        display_discard, display_pool);

	/* Set label */
	gtk_label_set_text(GTK_LABEL(game_status), buf);
}

/*
 * Draw phase indicators.
 */
void redraw_phase(void)
{
	int i;
	char buf[1024], *name;

	/* Loop over actions */
	for (i = ACT_EXPLORE_5_0; i <= ACT_PRODUCE; i++)
	{
		/* Skip second explore/consume actions */
		if (i == ACT_EXPLORE_1_1 || i == ACT_CONSUME_X2) continue;

		/* Get phase name */
		switch (i)
		{
			case ACT_EXPLORE_5_0: name = "Explore"; break;
			case ACT_DEVELOP:
			case ACT_DEVELOP2: name = "Develop"; break;
			case ACT_SETTLE:
			case ACT_SETTLE2: name = "Settle"; break;
			case ACT_CONSUME_TRADE: name = "Consume"; break;
			case ACT_PRODUCE: name = "Produce"; break;
			default: name = ""; break;
		}

		/* Check for basic game and advanced actions */
		if (!real_game.advanced &&
		    (i == ACT_DEVELOP2 || i == ACT_SETTLE2))
		{
			/* Simply hide label */
			gtk_widget_hide(phase_labels[i]);

			/* Next label */
			continue;
		}

		/* Check for inactive phase */
		else if (!real_game.action_selected[i])
		{
			/* Strikeout name */
			sprintf(buf, "<s>%s</s>", name);
		}

		/* Check for current phase */
		else if (real_game.cur_action == i)
		{
			/* Bold name */
			sprintf(buf,
			  "<span foreground=\"blue\" weight=\"bold\">%s</span>",
			  name);
		}

		/* Normal phase */
		else
		{
			/* Normal name */
			sprintf(buf, "%s", name);
		}

		/* Set label text */
		gtk_label_set_markup(GTK_LABEL(phase_labels[i]), buf);

		/* Show label */
		gtk_widget_show(phase_labels[i]);
	}
}

/*
 * Redraw everything.
 */
void redraw_everything(void)
{
	/* Redraw hand, table, and status area */
	redraw_status();
	redraw_hand();
	redraw_table();
	redraw_goal();
	redraw_phase();
}

/*
 * Resize status areas based on GUI options.
 */
static void status_resize(void)
{
	int i, w;

	/* Loop over original status areas */
	for (i = 0; i < MAX_PLAYER; i++)
	{
		/* Determine width to request */
		if (opt.shrink_opponent)
		{
			/* Request no width */
			w = 0;
		}
		else
		{
			/* Request enough width to show everything */
			w = -1;
		}

		/* Never request width for bottom row player */
		if (i == 0) w = 0;

		/* Request size */
		gtk_widget_set_size_request(orig_status[i], w, 35);
	}
}

/*
 * Request new height for table area, computed from area width.
 */
static void table_request(GtkWidget *widget, GtkRequisition *requisition,
                          gpointer data)
{
#if 0
	int req_height;

	/* Determine height to request */
	req_height = (widget->allocation.width / 4) * CARD_HEIGHT / CARD_WIDTH;

	/* Request height to match width */
	gtk_widget_set_size_request(widget, 0, req_height);
#endif

	/* Request smallest size possible */
	gtk_widget_set_size_request(widget, 0, 0);
}

/*
 * Request new height for hand area, computed from area width.
 */
static void hand_request(GtkWidget *widget, GtkRequisition *requisition,
                         gpointer data)
{
	int req_height;

	/* Determine height to request */
	req_height = (widget->allocation.width / 6) * CARD_HEIGHT / CARD_WIDTH;

	/* Add 10% for sliding cards up */
	req_height = 11 * req_height / 10;

	/* Request height to match width */
	gtk_widget_set_size_request(widget, 0, req_height);
}

/*
 * Hand area is re-allocated.
 */
static void hand_allocated(GtkWidget *widget, GtkAllocation *allocation,
                           gpointer data)
{
	static int old_width, old_height;

	/* Check for no difference from before */
	if (allocation->width == old_width && allocation->height == old_height)
	{
		/* Do nothing */
		return;
	}

	/* Remember current size */
	old_width = allocation->width;
	old_height = allocation->height;

	/* Redraw hand and table */
	redraw_hand();
	redraw_table();
}

/*
 * Goal area is re-allocated.
 */
static void goal_allocated(GtkWidget *widget, GtkAllocation *allocation,
                           gpointer data)
{
	static int old_width, old_height;

	/* Check for no difference from before */
	if (allocation->width == old_width && allocation->height == old_height)
	{
		/* Do nothing */
		return;
	}

	/* Remember current size */
	old_width = allocation->width;
	old_height = allocation->height;

	/* Redraw goal area */
	redraw_goal();
}

/*
 * Function to compare two cards in the hand for sorting.
 */
static int cmp_hand(const void *h1, const void *h2)
{
	displayed *i_ptr1 = (displayed *)h1, *i_ptr2 = (displayed *)h2;
	card *c_ptr1, *c_ptr2;

	/* Get card pointers */
	c_ptr1 = &real_game.deck[i_ptr1->index];
	c_ptr2 = &real_game.deck[i_ptr2->index];

	/* "Temp" cards always go after non-temp cards */
	if (c_ptr1->temp && !c_ptr2->temp) return 1;
	if (!c_ptr1->temp && c_ptr2->temp) return -1;

	/* Worlds come before developments */
	if (c_ptr1->d_ptr->type != c_ptr2->d_ptr->type)
	{
		/* Check for development */
		if (c_ptr1->d_ptr->type == TYPE_DEVELOPMENT) return 1;
		if (c_ptr2->d_ptr->type == TYPE_DEVELOPMENT) return -1;
	}

	/* Sort by cost */
	return c_ptr1->d_ptr->cost - c_ptr2->d_ptr->cost;
}

/*
 * Function to compare two cards on the table for sorting.
 */
static int cmp_table(const void *t1, const void *t2)
{
	displayed *i_ptr1 = (displayed *)t1, *i_ptr2 = (displayed *)t2;

	/* Sort by order played */
	return i_ptr1->order - i_ptr2->order;
}

/*
 * Reset our list of cards in hand.
 */
static void reset_hand(game *g, int color)
{
	displayed *i_ptr;
	card *c_ptr;
	int i;

	/* Clear size */
	hand_size = 0;

	/* Loop over cards in deck */
	for (i = 0; i < g->deck_size; i++)
	{
		/* Get card pointer */
		c_ptr = &g->deck[i];

		/* Skip unowned cards */
		if (c_ptr->owner != player_us) continue;

		/* Skip cards not in hand */
		if (c_ptr->where != WHERE_HAND) continue;

		/* Get next entry in hand list */
		i_ptr = &hand[hand_size++];

		/* Add card information */
		i_ptr->index = i;
		i_ptr->d_ptr = c_ptr->d_ptr;

		/* Card is in hand */
		i_ptr->hand = 1;

		/* Card is not eligible or selected */
		i_ptr->eligible = i_ptr->selected = 0;

		/* Card is not highlighted or gapped */
		i_ptr->highlight = i_ptr->gapped = 0;

		/* Set color flag */
		i_ptr->color = color;
	}

	/* Sort hand */
	qsort(hand, hand_size, sizeof(displayed), cmp_hand);
}

/*
 * Reset list of displayed cards on the table for the given player.
 */
static void reset_table(game *g, int who, int color)
{
	displayed *i_ptr;
	card *c_ptr;
	int i;

	/* Clear size */
	table_size[who] = 0;

	/* Loop over cards in deck */
	for (i = 0; i < g->deck_size; i++)
	{
		/* Get card pointer */
		c_ptr = &g->deck[i];

		/* Skip unowned cards */
		if (c_ptr->owner != who) continue;

		/* Skip cards not on table */
		if (c_ptr->where != WHERE_ACTIVE) continue;

		/* Get next entry in table list */
		i_ptr = &table[who][table_size[who]++];

		/* Add card information */
		i_ptr->index = i;
		i_ptr->d_ptr = c_ptr->d_ptr;

		/* Card is not in hand */
		i_ptr->hand = 0;

		/* Card is not eligible or selected */
		i_ptr->eligible = i_ptr->selected = 0;

		/* Set color flag */
		i_ptr->color = color;

		/* Check for good */
		i_ptr->covered = (c_ptr->covered != -1);

		/* Card is not highlighted or gapped */
		i_ptr->highlight = i_ptr->gapped = 0;

		/* Copy order played */
		i_ptr->order = c_ptr->order;
	}

	/* Sort list */
	qsort(table[who], table_size[who], sizeof(displayed), cmp_table);
}

/*
 * Reset status information for a player.
 */
static void reset_status(game *g, int who)
{
	int i;

	/* Copy player's name */
	strcpy(status_player[who].name, g->p[who].name);

	/* Check for actions known */
	if (g->cur_action >= ACT_SEARCH ||
	    count_active_flags(g, player_us, FLAG_SELECT_LAST))
	{
		/* Copy actions */
		status_player[who].action[0] = g->p[who].action[0];
		status_player[who].action[1] = g->p[who].action[1];
	}
	else
	{
		/* Actions aren't known */
		status_player[who].action[0] = ICON_NO_ACT;
		status_player[who].action[1] = ICON_NO_ACT;
	}

	/* Copy VP chips */
	status_player[who].vp = g->p[who].vp;
	status_player[who].end_vp = g->p[who].end_vp;

	/* Count cards in hand */
	status_player[who].cards_hand = count_player_area(g, who, WHERE_HAND);

	/* Copy prestige */
	status_player[who].prestige = g->p[who].prestige;

	/* Count military strength */
	status_player[who].military = total_military(g, who);

	/* Get text of military tooltip */
	strcpy(status_player[who].military_tip, get_military_tooltip(g, who));

	/* Loop over goals */
	for (i = 0; i < MAX_GOAL; i++)
	{
		/* Assume goal is not displayed */
		status_player[who].goal_display[i] = 0;

		/* Assume goal is not grayed */
		status_player[who].goal_gray[i] = 0;

		/* Skip inactive goals */
		if (!g->goal_active[i]) continue;

		/* Check for "first" goal */
		if (i <= GOAL_FIRST_4_MILITARY)
		{
			/* Check for unclaimed */
			if (!g->p[who].goal_claimed[i]) continue;
		}
		else
		{
			/* Check for insufficient progress */
			if (g->p[who].goal_progress[i] < goal_minimum(i))
				continue;

			/* Check for less progress than other players */
			if (g->p[who].goal_progress[i] < g->goal_most[i])
				continue;

			/* Unclaimed goals should be gray */
			if (!g->p[who].goal_claimed[i])
				status_player[who].goal_gray[i] = 1;
		}

		/* Goal should be displayed */
		status_player[who].goal_display[i] = 1;

		/* Get text of goal tooltip */
		strcpy(status_player[who].goal_tip[i], goal_tooltip(g, i));
	}
}

/*
 * Reset our hand list, and all players' table lists.
 */
void reset_cards(game *g, int color_hand, int color_table)
{
	card *c_ptr;
	int i;

	/* Score game */
	score_game(g);

	/* Reset hand */
	reset_hand(g, color_hand);

	/* Loop over players */
	for (i = 0; i < real_game.num_players; i++)
	{
		/* Reset table of player */
		reset_table(g, i, color_table);

		/* Reset status information for player */
		reset_status(g, i);
	}

	/* Clear displayed status info */
	display_deck = 0;
	display_discard = 0;

	/* Loop over cards */
	for (i = 0; i < g->deck_size; i++)
	{
		/* Get card pointer */
		c_ptr = &g->deck[i];

		/* Check for card in draw pile */
		if (c_ptr->where == WHERE_DECK) display_deck++;

		/* Check for card in discard pile */
		if (c_ptr->where == WHERE_DISCARD) display_discard++;
	}

	/* Get chips in VP pool */
	display_pool = g->vp_pool;

	/* Do not display negative numbers */
	if (display_pool < 0) display_pool = 0;
}

/*
 * Number of action buttons pressed.
 */
static int actions_chosen;

/*
 * Action which is receiving prestige boost.
 */
static int prestige_action;

/*
 * Action button widgets.
 */
static GtkWidget *action_toggle[MAX_ACTION];


/*
 * Reset an action button's image to the given icon.
 */
static void reset_action_icon(GtkWidget *button, int act)
{
	GtkWidget *image;
	GdkPixbuf *buf;
	int h;

	/* Get previous image */
	image = gtk_bin_get_child(GTK_BIN(button));

	/* Get pixbuf of image */
	buf = gtk_image_get_pixbuf(GTK_IMAGE(image));

	/* Get image height */
	h = gdk_pixbuf_get_height(buf);

	/* Remove previous image */
	gtk_widget_destroy(image);

	/* Get image for button */
	image = action_icon(act, h);

	/* Show new image */
	gtk_widget_show(image);

	/* Add image to button */
	gtk_container_add(GTK_CONTAINER(button), image);
}

/*
 * Toggle a prestige boost to a chosen action.
 */
static void prestige_pressed(GtkButton *button, gpointer data)
{
	GtkWidget *toggle;
	int i;

	/* Check for search pressed */
	if (gtk_toggle_button_get_active(
	                          GTK_TOGGLE_BUTTON(action_toggle[ACT_SEARCH])))
	{
		/* Do nothing */
		return;
	}

	/* Check for current prestige action */
	if (prestige_action != -1)
	{
		/* Get button for old prestige action */
		toggle = action_toggle[prestige_action];

		/* Reset icon to non-prestige version */
		reset_action_icon(toggle, prestige_action);
	}

	/* Loop over actions */
	for (i = prestige_action + 1; i < MAX_ACTION; i++)
	{
		/* Skip search action */
		if (i == ACT_SEARCH) continue;

		/* Get button for this action */
		toggle = action_toggle[i];

		/* Skip unselected */
		if (!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(toggle)))
		{
			/* Skip action */
			continue;
		}

		/* Set prestige action */
		prestige_action = i;

		/* Reset icon image */
		reset_action_icon(toggle, i | ACT_PRESTIGE);

		/* Done */
		return;
	}

	/* Check for no action to boost */
	if (i == MAX_ACTION) prestige_action = -1;
}

/*
 * Callback when action choice changes.
 */
static void action_choice_changed(GtkToggleButton *button, gpointer data)
{
	int i = GPOINTER_TO_INT(data), j;

	/* Check for toggled button */
	if (gtk_toggle_button_get_active(button))
	{
		/* Increment count of buttons pressed */
		actions_chosen++;

		/* Check for pressed search button */
		if (i == ACT_SEARCH)
		{
			/* Clear prestige action, if any */
			if (prestige_action != -1)
			{
				/* Get current prestige action */
				j = prestige_action;

				/* Reset icon */
				reset_action_icon(action_toggle[j], j);

				/* Clear prestige action */
				prestige_action = -1;
			}
		}
	}
	else
	{
		/* Decrement count of buttons pressed */
		actions_chosen--;
	}

	/* Check for prestige action toggled */
	if (i == prestige_action)
	{
		/* Clear prestige boost */
		prestige_action = -1;

		/* Reset icon to non-prestige version */
		reset_action_icon(GTK_WIDGET(button), i);
	}

	/* Check for exactly 2 actions chosen */
	gtk_widget_set_sensitive(action_button, actions_chosen == 2);
}

/*
 * Callback when an action button's key is pressed.
 */
static void action_keyed(GtkWidget *widget, gpointer data)
{
	/* Change button state */
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(widget),
	              !gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget)));
}


/*
 * Choose two actions.
 */
static void gui_choose_action_advanced(game *g, int who, int action[2], int one)
{
	GtkWidget *prestige = NULL, *image, *label;
	int i, a, h, n = 0, key = GDK_1;

	/* Deactivate action button */
	gtk_widget_set_sensitive(action_button, FALSE);

	/* Clear count of buttons chosen */
	actions_chosen = 0;

	/* Check for needing only one action */
	if (one == 1) actions_chosen = 1;

	/* Reset displayed cards */
	reset_cards(g, TRUE, TRUE);

	/* Redraw everything */
	redraw_everything();

	/* Set prompt */
	gtk_label_set_text(GTK_LABEL(action_prompt), "Choose Actions");

	/* Check for needing only first/second action */
	if (one == 1)
	{
		/* Set prompt */
		gtk_label_set_text(GTK_LABEL(action_prompt),
		                   "Choose first Action");
	}
	else if (one == 2)
	{
		/* Set prompt */
		gtk_label_set_text(GTK_LABEL(action_prompt),
		                   "Choose second Action");
	}

	/* Get height of action box */
	h = action_box->allocation.height - 10;

	/* Loop over actions */
	for (i = 0; i < MAX_ACTION; i++)
	{
		/* Check for unusable search action */
		if (i == ACT_SEARCH && (g->expanded < 3 ||
		                        g->p[who].prestige_action_used ||
					(one == 2 &&
					 g->p[who].action[0] & ACT_PRESTIGE)))
		{
			/* Clear toggle button */
			action_toggle[i] = NULL;

			/* Skip search action */
			continue;
		}

		/* Create toggle button */
		action_toggle[i] = gtk_toggle_button_new();

		/* Get action index */
		a = i;

		/* Check for second Develop or Settle */
		if (a == ACT_DEVELOP2) a = ACT_DEVELOP;
		if (a == ACT_SETTLE2) a = ACT_SETTLE;

		/* Get icon for action */
		image = action_icon(a, h);

		/* Do not request height */
		gtk_widget_set_size_request(image, -1, 0);

		/* Pack image into button */
		gtk_container_add(GTK_CONTAINER(action_toggle[i]), image);

		/* Pack button into action box */
		gtk_box_pack_start(GTK_BOX(action_box), action_toggle[i], FALSE,
		                   FALSE, 0);

		/* Create tooltip for button */
		gtk_widget_set_tooltip_text(action_toggle[i], action_name(i));

		/* Add handler for keypresses */
		gtk_widget_add_accelerator(action_toggle[i], "key-signal",
		                           window_accel, key++, 0, 0);

		/* Connect "toggled" signal */
		g_signal_connect(G_OBJECT(action_toggle[i]), "toggled",
		                 G_CALLBACK(action_choice_changed),
		                 GINT_TO_POINTER(i));

		/* Connect "pointer enter" signal */
		g_signal_connect(G_OBJECT(action_toggle[i]),
		                 "enter-notify-event",
		                 G_CALLBACK(redraw_action), GINT_TO_POINTER(a));

		/* Connect key-signal */
		g_signal_connect(G_OBJECT(action_toggle[i]), "key-signal",
		                 G_CALLBACK(action_keyed), NULL);

		/* Show everything */
		gtk_widget_show_all(action_toggle[i]);

		/* Check for choosing second action and this was first */
		if (one == 2 && (g->p[player_us].action[0] & ACT_MASK) == i)
		{
			/* Press button */
			gtk_toggle_button_set_active(
			            GTK_TOGGLE_BUTTON(action_toggle[i]), TRUE);

			/* Do not allow user to press button */
			gtk_widget_set_sensitive(action_toggle[i], FALSE);

			/* Check for prestige action */
			if (g->p[player_us].action[0] & ACT_PRESTIGE)
			{
				/* Reset icon */
				reset_action_icon(action_toggle[i],
				                  i | ACT_PRESTIGE);
			}
		}
	}

	/* Do not boost any action yet */
	prestige_action = -1;

	/* Check for forced first action */
	if (one == 2)
	{
		/* Check for first action as prestige */
		if (g->p[player_us].action[0] & ACT_PRESTIGE)
		{
			/* Mark prestige action */
			prestige_action = g->p[player_us].action[0] & ACT_MASK;
		}
	}

	/* Check for usable prestige action */
	if (real_game.expanded >= 3 && !real_game.p[who].prestige_action_used &&
	    real_game.p[who].prestige > 0 && prestige_action == -1)
	{
		/* Create button to toggle prestige */
		prestige = gtk_button_new();

		/* Get icon for action */
		image = action_icon(ICON_PRESTIGE, h);

		/* Do not request height */
		gtk_widget_set_size_request(image, -1, 0);

		/* Pack image into button */
		gtk_container_add(GTK_CONTAINER(prestige), image);

		/* Pack button into action box */
		gtk_box_pack_start(GTK_BOX(action_box), prestige, FALSE,
		                   FALSE, h);

		/* Connect "pointer enter" signal */
		g_signal_connect(G_OBJECT(prestige), "enter-notify-event",
		                 G_CALLBACK(redraw_action), GINT_TO_POINTER(0));

		/* Connect "pressed" signal */
		g_signal_connect(G_OBJECT(prestige), "pressed",
		                 G_CALLBACK(prestige_pressed), NULL);

		/* Show everything */
		gtk_widget_show_all(prestige);
	}

	/* Create filler label */
	label = gtk_label_new("");

	/* Add label after action buttons */
	gtk_box_pack_start(GTK_BOX(action_box), label, TRUE, TRUE, 0);

	/* Show label */
	gtk_widget_show(label);

	/* Process events */
	gtk_main();

	/* Check for real selection made */
	if (!restart_loop)
	{
		/* Save state for future undo */
		undo_state = real_game;
	}

	/* Loop over choices */
	for (i = 0; i < MAX_ACTION; i++)
	{
		/* Skip unavailable actions */
		if (!action_toggle[i]) continue;

		/* Check for active */
		if (gtk_toggle_button_get_active(
		                          GTK_TOGGLE_BUTTON(action_toggle[i])))
		{
			/* Check for prestige action */
			if (i == prestige_action)
			{
				/* Mark prestige as chosen */
				action[n++] = i | ACT_PRESTIGE;
			}
			else
			{
				/* Set choice */
				action[n++] = i;
			}
		}
	}

	/* Destroy buttons */
	for (i = 0; i < MAX_ACTION; i++)
	{
		/* Skip unavailable actions */
		if (!action_toggle[i]) continue;

		/* Destroy button */
		gtk_widget_destroy(action_toggle[i]);
	}

	/* Destroy filler label */
	gtk_widget_destroy(label);

	/* Destroy prestige button if created */
	if (prestige) gtk_widget_destroy(prestige);

	/* Check for second Develop chosen without first */
	if ((action[0] & ACT_MASK) == ACT_DEVELOP2)
		action[0] = ACT_DEVELOP | (action[0] & ACT_PRESTIGE);
	if ((action[1] & ACT_MASK) == ACT_DEVELOP2 &&
	    (action[0] & ACT_MASK) != ACT_DEVELOP)
		action[1] = ACT_DEVELOP | (action[1] & ACT_PRESTIGE);

	/* Check for second Settle chosen without first */
	if ((action[0] & ACT_MASK) == ACT_SETTLE2)
		action[0] = ACT_SETTLE | (action[0] & ACT_PRESTIGE);
	if ((action[1] & ACT_MASK) == ACT_SETTLE2 &&
	    (action[0] & ACT_MASK) != ACT_SETTLE)
		action[1] = ACT_SETTLE | (action[1] & ACT_PRESTIGE);
}

/*
 * Choose action card.
 */
void gui_choose_action(game *g, int who, int action[2], int one)
{
	GtkWidget *button[MAX_ACTION], *image, *label, *group;
	GtkWidget *prestige = NULL;
	int i, h, key = GDK_1;

	/* Set save state */
	save_state = real_game;

	/* Check for advanced game */
	if (real_game.advanced)
	{
		/* Call advanced function instead */
		return gui_choose_action_advanced(g, who, action, one);
	}

	/* Activate action button */
	gtk_widget_set_sensitive(action_button, TRUE);

	/* Reset displayed cards */
	reset_cards(g, TRUE, TRUE);

	/* Redraw everything */
	redraw_everything();

	/* Set prompt */
	gtk_label_set_text(GTK_LABEL(action_prompt), "Choose Action");

	/* Clear grouping of buttons */
	group = NULL;

	/* Get height of action box */
	h = action_box->allocation.height - 10;

	/* Loop over actions */
	for (i = 0; i < MAX_ACTION; i++)
	{
		/* Clear button pointer */
		button[i] = NULL;

		/* Check for unusable search action */
		if (i == ACT_SEARCH && (real_game.expanded < 3 ||
		                        real_game.p[who].prestige_action_used))
		{
			/* Skip search action */
			continue;
		}

		/* Skip second develop/settle */
		if (i == ACT_DEVELOP2 || i == ACT_SETTLE2) continue;

		/* Create radio button */
		button[i] = gtk_radio_button_new_from_widget(
		                                   GTK_RADIO_BUTTON(group));

		/* Remember grouping */
		group = button[i];

		/* Get icon for action */
		image = action_icon(i, h);

		/* Do not request height */
		gtk_widget_set_size_request(image, -1, 0);

		/* Draw button without separate indicator */
		gtk_toggle_button_set_mode(GTK_TOGGLE_BUTTON(button[i]), FALSE);

		/* Pack image into button */
		gtk_container_add(GTK_CONTAINER(button[i]), image);

		/* Pack button into action box */
		gtk_box_pack_start(GTK_BOX(action_box), button[i], FALSE,
		                   FALSE, 0);

		/* Create tooltip for button */
		gtk_widget_set_tooltip_text(button[i], action_name(i));

		/* Add handler for keypresses */
		gtk_widget_add_accelerator(button[i], "key-signal",
		                           window_accel, key++, 0, 0);

		/* Connect "pointer enter" signal */
		g_signal_connect(G_OBJECT(button[i]), "enter-notify-event",
		                 G_CALLBACK(redraw_action), GINT_TO_POINTER(i));

		/* Connect key-signal */
		g_signal_connect(G_OBJECT(button[i]), "key-signal",
		                 G_CALLBACK(action_keyed), NULL);

		/* Show everything */
		gtk_widget_show_all(button[i]);
	}

	/* Check for usable prestige action */
	if (real_game.expanded >= 3 && !real_game.p[who].prestige_action_used &&
	    real_game.p[who].prestige > 0)
	{
		/* Create toggle button for prestige */
		prestige = gtk_toggle_button_new();

		/* Get icon for action */
		image = action_icon(ICON_PRESTIGE, h);

		/* Do not request height */
		gtk_widget_set_size_request(image, -1, 0);

		/* Pack image into button */
		gtk_container_add(GTK_CONTAINER(prestige), image);

		/* Pack button into action box */
		gtk_box_pack_start(GTK_BOX(action_box), prestige, FALSE,
		                   FALSE, h);

		/* Connect "pointer enter" signal */
		g_signal_connect(G_OBJECT(prestige), "enter-notify-event",
		                 G_CALLBACK(redraw_action), GINT_TO_POINTER(0));

		/* Show everything */
		gtk_widget_show_all(prestige);
	}

	/* Create filler label */
	label = gtk_label_new("");

	/* Add label after action buttons */
	gtk_box_pack_start(GTK_BOX(action_box), label, TRUE, TRUE, 0);

	/* Show label */
	gtk_widget_show(label);

	/* Process events */
	gtk_main();

	/* Check for real selection made */
	if (!restart_loop)
	{
		/* Save state for future undo */
		undo_state = real_game;
	}

	/* Loop over choices */
	for (i = 0; i < MAX_ACTION; i++)
	{
		/* Skip uncreated buttons */
		if (button[i] == NULL) continue;

		/* Check for active */
		if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(button[i])))
		{
			/* Set choice */
			action[0] = i;
			action[1] = -1;
		}
	}

	/* Check for prestige button available */
	if (prestige)
	{
		/* Check for pressed */
		if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(prestige)) &&
		    action[0] != ACT_SEARCH)
		{
			/* Add prestige flag to action */
			action[0] |= ACT_PRESTIGE;
		}

		/* Destroy prestige button */
		gtk_widget_destroy(prestige);
	}

	/* Destroy buttons */
	for (i = 0; i < MAX_ACTION; i++)
	{
		/* Skip uncreated buttons */
		if (button[i] == NULL) continue;

		/* Destroy button */
		gtk_widget_destroy(button[i]);
	}

	/* Destroy filler label */
	gtk_widget_destroy(label);
}

/*
 * Choose a start world from those given.
 */
void gui_choose_start(game *g, int who, int list[], int *num, int special[],
                      int *num_special)
{
	char buf[1024];
	displayed *i_ptr;
	card *c_ptr;
	int i, j, n = 0;

	/* Create prompt */
	sprintf(buf, "Choose start world and hand discards");

	/* Set prompt */
	gtk_label_set_text(GTK_LABEL(action_prompt), buf);

	/* Set restrictions on action button */
	action_restrict = RESTRICT_START;

	/* Deactivate action button */
	gtk_widget_set_sensitive(action_button, FALSE);

	/* Reset displayed cards */
	reset_cards(g, TRUE, TRUE);

	/* Add start worlds to table */
	for (i = 0; i < *num_special; i++)
	{
		/* Get card pointer */
		c_ptr = &real_game.deck[special[i]];

		/* Get next entry in table list */
		i_ptr = &table[player_us][table_size[player_us]++];

		/* Add card information */
		i_ptr->index = special[i];
		i_ptr->d_ptr = c_ptr->d_ptr;

		/* Card is not in hand */
		i_ptr->hand = 0;

		/* Card is eligible */
		i_ptr->eligible = 1;

		/* Card should be highlighted when selected */
		i_ptr->highlight = 1;
		
		/* Card is not selected */
		i_ptr->selected = 0;

		/* Card is not covered */
		i_ptr->covered = 0;
	}

	/* Loop over cards in list */
	for (i = 0; i < *num; i++)
	{
		/* Loop over cards in hand */
		for (j = 0; j < hand_size; j++)
		{
			/* Get hand pointer */
			i_ptr = &hand[j];

			/* Check for matching index */
			if (i_ptr->index == list[i])
			{
				/* Card is eligible */
				i_ptr->eligible = 1;
			}
		}
	}

	/* Redraw everything */
	redraw_everything();

	/* Process events */
	gtk_main();

	/* Loop over table cards */
	for (i = 0; i < table_size[player_us]; i++)
	{
		/* Get displayed card pointer */
		i_ptr = &table[player_us][i];

		/* Check for selected start world */
		if (i_ptr->selected)
		{
			/* Remember start world */
			special[0] = i_ptr->index;
			*num_special = 1;
		}
	}

	/* Loop over cards in hand */
	for (i = 0; i < hand_size; i++)
	{
		/* Get hand pointer */
		i_ptr = &hand[i];

		/* Check for selected */
		if (i_ptr->selected)
		{
			/* Add to list */
			list[n++] = i_ptr->index;
		}
	}

	/* Set number of cards selected */
	*num = n;
}

/*
 * Ask the player to discard some number of cards from the set given.
 */
void gui_choose_discard(game *g, int who, int list[], int *num, int discard)
{
	char buf[1024];
	displayed *i_ptr;
	int i, j, n = 0;

	/* Create prompt */
	sprintf(buf, "Choose %d cards to discard", discard);

	/* Set prompt */
	gtk_label_set_text(GTK_LABEL(action_prompt), buf);

	/* Set restrictions on action button */
	action_restrict = RESTRICT_NUM;
	action_min = action_max = discard;

	/* Deactivate action button */
	gtk_widget_set_sensitive(action_button, FALSE);

	/* Reset displayed cards */
	reset_cards(g, FALSE, TRUE);

	/* Loop over cards in list */
	for (i = 0; i < *num; i++)
	{
		/* Loop over cards in hand */
		for (j = 0; j < hand_size; j++)
		{
			/* Get hand pointer */
			i_ptr = &hand[j];

			/* Check for matching index */
			if (i_ptr->index == list[i])
			{
				/* Card is eligible */
				i_ptr->eligible = 1;
			}
		}
	}

	/* Redraw everything */
	redraw_everything();

	/* Process events */
	gtk_main();

	/* Loop over cards in hand */
	for (i = 0; i < hand_size; i++)
	{
		/* Get hand pointer */
		i_ptr = &hand[i];

		/* Check for selected */
		if (i_ptr->selected)
		{
			/* Add to list */
			list[n++] = i_ptr->index;
		}
	}

	/* Set number of cards selected */
	*num = n;
}

/*
 * Ask the player to save on of the given cards under a world.
 */
void gui_choose_save(game *g, int who, int list[], int *num)
{
	char buf[1024];
	displayed *i_ptr;
	card *c_ptr;
	int i, j, n = 0;

	/* Create prompt */
	sprintf(buf, "Choose card to save for later");

	/* Set prompt */
	gtk_label_set_text(GTK_LABEL(action_prompt), buf);

	/* Set restrictions on action button */
	action_restrict = RESTRICT_NUM;
	action_min = action_max = 1;

	/* Deactivate action button */
	gtk_widget_set_sensitive(action_button, FALSE);

	/* Reset displayed cards */
	reset_cards(g, FALSE, TRUE);

	/* Loop over choices */
	for (i = 0; i < *num; i++)
	{
		/* Loop over cards in hand already */
		for (j = 0; j < hand_size; j++)
		{
			/* Get displayed card */
			i_ptr = &hand[j];

			/* Check for match */
			if (i_ptr->index == list[i])
			{
				/* Mark card as eligible */
				i_ptr->eligible = 1;
				i_ptr->gapped = 1;
				i_ptr->highlight = 1;
				break;
			}
		}

		/* Check for card already found */
		if (j < hand_size) continue;

		/* Get card pointer */
		c_ptr = &real_game.deck[list[i]];

		/* Get next entry in hand list */
		i_ptr = &hand[hand_size++];

		/* Add card information */
		i_ptr->index = list[i];
		i_ptr->d_ptr = c_ptr->d_ptr;

		/* Card is in hand */
		i_ptr->hand = 1;

		/* Card is eligible for selection */
		i_ptr->eligible = 1;
		i_ptr->gapped = 1;
		i_ptr->highlight = 1;

		/* Card is not selected */
		i_ptr->selected = 0;
	}

	/* Redraw everything */
	redraw_everything();

	/* Process events */
	gtk_main();

	/* Loop over cards in hand */
	for (i = 0; i < hand_size; i++)
	{
		/* Get hand pointer */
		i_ptr = &hand[i];

		/* Check for selected */
		if (i_ptr->selected)
		{
			/* Add to list */
			list[n++] = i_ptr->index;
		}
	}

	/* Set number of cards selected */
	*num = n;
}

/*
 * Simulate Explore phase.
 *
 * We need to do nothing.
 */
void gui_explore_sample(game *g, int who, int draw, int keep, int discard_any)
{
}

/*
 * Choose whether to discard a card for prestige.
 */
void gui_choose_discard_prestige(game *g, int who, int list[], int *num)
{
	char buf[1024];
	displayed *i_ptr;
	int i, j, n = 0;

	/* Create prompt */
	sprintf(buf, "Choose cards to discard for prestige");

	/* Set prompt */
	gtk_label_set_text(GTK_LABEL(action_prompt), buf);

	/* Set restrictions on action button */
	action_restrict = RESTRICT_NUM;
	action_min = 0;
	action_max = 1;

	/* Deactivate action button */
	gtk_widget_set_sensitive(action_button, FALSE);

	/* Reset displayed cards */
	reset_cards(g, FALSE, TRUE);

	/* Loop over cards in list */
	for (i = 0; i < *num; i++)
	{
		/* Loop over cards in hand */
		for (j = 0; j < hand_size; j++)
		{
			/* Get hand pointer */
			i_ptr = &hand[j];

			/* Check for matching index */
			if (i_ptr->index == list[i])
			{
				/* Card is eligible */
				i_ptr->eligible = 1;
			}
		}
	}

	/* Redraw everything */
	redraw_everything();

	/* Process events */
	gtk_main();

	/* Loop over cards in hand */
	for (i = 0; i < hand_size; i++)
	{
		/* Get hand pointer */
		i_ptr = &hand[i];

		/* Check for selected */
		if (i_ptr->selected)
		{
			/* Add to list */
			list[n++] = i_ptr->index;
		}
	}

	/* Set number of cards selected */
	*num = n;
}

/*
 * Choose a card to place for the Develop or Settle phases.
 */
int gui_choose_place(game *g, int who, int list[], int num, int phase,
                     int special)
{
	char buf[1024];
	displayed *i_ptr;
	int i, j;

	/* Create prompt */
	sprintf(buf, "Choose card to %s",
	        phase == PHASE_DEVELOP ? "develop" : "settle");

	/* Check for special card used to provide power */
	if (special != -1)
	{
		/* Append name to prompt */
		strcat(buf, " using ");
		strcat(buf, g->deck[special].d_ptr->name);
	}

	/* Set prompt */
	gtk_label_set_text(GTK_LABEL(action_prompt), buf);

	/* Set restrictions on action button */
	action_restrict = RESTRICT_NUM;
	action_min = 0;
	action_max = 1;

	/* Activate action button */
	gtk_widget_set_sensitive(action_button, TRUE);

	/* Reset displayed cards */
	reset_cards(g, FALSE, TRUE);

	/* Loop over cards in list */
	for (i = 0; i < num; i++)
	{
		/* Loop over cards in hand */
		for (j = 0; j < hand_size; j++)
		{
			/* Get hand pointer */
			i_ptr = &hand[j];

			/* Check for matching index */
			if (i_ptr->index == list[i])
			{
				/* Card is eligible */
				i_ptr->eligible = 1;

				/* Card should be highlighted when selected */
				i_ptr->highlight = 1;
			}
		}
	}

	/* Redraw everything */
	redraw_everything();

	/* Process events */
	gtk_main();

	/* Loop over cards in hand */
	for (i = 0; i < hand_size; i++)
	{
		/* Get hand pointer */
		i_ptr = &hand[i];

		/* Check for selected */
		if (i_ptr->selected)
		{
			/* Return selection */
			return i_ptr->index;
		}
	}

	/* No selection made */
	return -1;
}

/*
 * Choose method of payment for a placed card.
 *
 * We include some active cards that have powers that can be triggered,
 * such as the Contact Specialist or Colony Ship.
 */
void gui_choose_pay(game *g, int who, int which, int list[], int *num,
                    int special[], int *num_special, int mil_only)
{
	card *c_ptr;
	displayed *i_ptr;
	char buf[1024];
	int i, j, n = 0, ns = 0;

	/* Get card we are paying for */
	c_ptr = &real_game.deck[which];

	/* Create prompt */
	sprintf(buf, "Choose payment for %s", c_ptr->d_ptr->name);

	/* Set prompt */
	gtk_label_set_text(GTK_LABEL(action_prompt), buf);

	/* Reset displayed cards */
	reset_cards(g, FALSE, FALSE);

	/* Set button restriction */
	action_restrict = RESTRICT_PAY;
	action_payment_which = which;
	action_payment_mil = mil_only;

	/* Deactivate action button */
	gtk_widget_set_sensitive(action_button, action_check_payment());

	/* Loop over cards in list */
	for (i = 0; i < *num; i++)
	{
		/* Loop over cards in hand */
		for (j = 0; j < hand_size; j++)
		{
			/* Get hand pointer */
			i_ptr = &hand[j];

			/* Check for matching index */
			if (i_ptr->index == list[i])
			{
				/* Card is eligible */
				i_ptr->eligible = 1;
			}
		}
	}

	/* Loop over special cards */
	for (i = 0; i < *num_special; i++)
	{
		/* Loop over cards on table */
		for (j = 0; j < table_size[player_us]; j++)
		{
			/* Get table card pointer */
			i_ptr = &table[player_us][j];

			/* Check for matching index */
			if (i_ptr->index == special[i])
			{
				/* Card is eligible */
				i_ptr->eligible = 1;

				/* Card should be highlighted when selected */
				i_ptr->highlight = 1;
			}
		}
	}

	/* Redraw everything */
	redraw_everything();

	/* Process events */
	gtk_main();

	/* Loop over cards in hand */
	for (i = 0; i < hand_size; i++)
	{
		/* Get hand pointer */
		i_ptr = &hand[i];

		/* Check for selected */
		if (i_ptr->selected)
		{
			/* Add to list */
			list[n++] = i_ptr->index;
		}
	}

	/* Set number of cards selected */
	*num = n;

	/* Loop over cards on table */
	for (i = 0; i < table_size[player_us]; i++)
	{
		/* Get table card pointer */
		i_ptr = &table[player_us][i];

		/* Check for selected */
		if (i_ptr->selected)
		{
			/* Add to list */
			special[ns++] = i_ptr->index;
		}
	}

	/* Set number of special cards selected */
	*num_special = ns;
}

/*
 * Choose a world to attempt a takeover of.
 *
 * We must also choose a card showing a takeover power to use.
 */
int gui_choose_takeover(game *g, int who, int list[], int *num,
                        int special[], int *num_special)
{
	displayed *i_ptr;
	char buf[1024];
	int i, j, k, target = -1;

	/* Create prompt */
	sprintf(buf, "Choose world to takeover and power to use");

	/* Set prompt */
	gtk_label_set_text(GTK_LABEL(action_prompt), buf);

	/* Reset displayed cards */
	reset_cards(g, FALSE, FALSE);

	/* Set button restriction */
	action_restrict = RESTRICT_TAKEOVER;

	/* Activate action button */
	gtk_widget_set_sensitive(action_button, TRUE);

	/* Loop over cards in list */
	for (i = 0; i < *num; i++)
	{
		/* Loop over opponents */
		for (j = 0; j < g->num_players; j++)
		{
			/* Skip our own cards */
			if (j == player_us) continue;

			/* Loop over opponent's table cards */
			for (k = 0; k < table_size[j]; k++)
			{
				/* Get displayed card's pointer */
				i_ptr = &table[j][k];

				/* Check for matching index */
				if (i_ptr->index == list[i])
				{
					/* Card is eligible */
					i_ptr->eligible = 1;
					i_ptr->highlight = 1;
				}
			}
		}
	}

	/* Loop over special cards */
	for (i = 0; i < *num_special; i++)
	{
		/* Loop over cards on table */
		for (j = 0; j < table_size[player_us]; j++)
		{
			/* Get table card pointer */
			i_ptr = &table[player_us][j];

			/* Check for matching index */
			if (i_ptr->index == special[i])
			{
				/* Card is eligible */
				i_ptr->eligible = 1;

				/* Card should be highlighted when selected */
				i_ptr->highlight = 1;
			}
		}
	}

	/* Redraw everything */
	redraw_everything();

	/* Process events */
	gtk_main();

	/* Loop over cards on table */
	for (i = 0; i < table_size[player_us]; i++)
	{
		/* Get table card pointer */
		i_ptr = &table[player_us][i];

		/* Check for selected */
		if (i_ptr->selected)
		{
			/* Use this card's takeover power */
			special[0] = i_ptr->index;
		}
	}

	/* Set number of special cards selected */
	*num_special = 1;

	/* Loop over opponents */
	for (i = 0; i < g->num_players; i++)
	{
		/* Skip our own cards */
		if (i == player_us) continue;

		/* Loop over opponent's table cards */
		for (j = 0; j < table_size[i]; j++)
		{
			/* Get displayed card's pointer */
			i_ptr = &table[i][j];

			/* Check for selected */
			if (i_ptr->selected)
			{
				/* Remember target */
				target = i_ptr->index;
			}
		}
	}

	/* Return target */
	return target;
}

/*
 * Choose a method to defend against a takeover.
 */
void gui_choose_defend(game *g, int who, int which, int opponent, int deficit,
                       int list[], int *num, int special[], int *num_special)
{
	card *c_ptr;
	displayed *i_ptr;
	char buf[1024];
	int i, j, n = 0, ns = 0;

	/* Get card we are defending */
	c_ptr = &real_game.deck[which];

	/* Create prompt */
	sprintf(buf, "Choose defense for %s (need %d extra military)",
	        c_ptr->d_ptr->name, deficit + 1);

	/* Set prompt */
	gtk_label_set_text(GTK_LABEL(action_prompt), buf);

	/* Reset displayed cards */
	reset_cards(g, FALSE, TRUE);

	/* Set button restriction */
	action_restrict = RESTRICT_DEFEND;

	/* Deactivate action button */
	gtk_widget_set_sensitive(action_button, TRUE);

	/* Loop over cards in list */
	for (i = 0; i < *num; i++)
	{
		/* Loop over cards in hand */
		for (j = 0; j < hand_size; j++)
		{
			/* Get hand pointer */
			i_ptr = &hand[j];

			/* Check for matching index */
			if (i_ptr->index == list[i])
			{
				/* Card is eligible */
				i_ptr->eligible = 1;
			}
		}
	}

	/* Loop over special cards */
	for (i = 0; i < *num_special; i++)
	{
		/* Loop over cards on table */
		for (j = 0; j < table_size[player_us]; j++)
		{
			/* Get table card pointer */
			i_ptr = &table[player_us][j];

			/* Check for matching index */
			if (i_ptr->index == special[i])
			{
				/* Card is eligible */
				i_ptr->eligible = 1;

				/* Card should be highlighted when selected */
				i_ptr->highlight = 1;
			}
		}
	}

	/* Redraw everything */
	redraw_everything();

	/* Process events */
	gtk_main();

	/* Loop over cards in hand */
	for (i = 0; i < hand_size; i++)
	{
		/* Get hand pointer */
		i_ptr = &hand[i];

		/* Check for selected */
		if (i_ptr->selected)
		{
			/* Add to list */
			list[n++] = i_ptr->index;
		}
	}

	/* Set number of cards selected */
	*num = n;

	/* Loop over cards on table */
	for (i = 0; i < table_size[player_us]; i++)
	{
		/* Get table card pointer */
		i_ptr = &table[player_us][i];

		/* Check for selected */
		if (i_ptr->selected)
		{
			/* Add to list */
			special[ns++] = i_ptr->index;
		}
	}

	/* Set number of special cards selected */
	*num_special = ns;
}

/*
 * Choose which takeover, if any, to prevent.
 */
void gui_choose_takeover_prevent(game *g, int who, int list[], int *num,
                                 int special[])
{
	GtkWidget *combo;
	card *c_ptr, *b_ptr;
	char buf[1024];
	int i;

	/* Activate action button */
	gtk_widget_set_sensitive(action_button, TRUE);

	/* Reset displayed cards */
	reset_cards(g, TRUE, TRUE);

	/* Redraw everything */
	redraw_everything();

	/* Create prompt */
	sprintf(buf, "Choose takeover to prevent");

	/* Set prompt */
	gtk_label_set_text(GTK_LABEL(action_prompt), buf);

	/* Create simple combo box */
	combo = gtk_combo_box_new_text();

	/* Loop over powers */
	for (i = 0; i < *num; i++)
	{
		/* Get target world */
		c_ptr = &g->deck[list[i]];

		/* Get card holding takeover power being used */
		b_ptr = &g->deck[special[i]];

		/* Format choice */
		sprintf(buf, "%s using %s", c_ptr->d_ptr->name,
		                            b_ptr->d_ptr->name);

		/* Append option to combo box */
		gtk_combo_box_append_text(GTK_COMBO_BOX(combo), buf);
	}

	/* Add choice for no prevention */
	sprintf(buf, "None (allow all takeovers)");

	/* Append option to combo box */
	gtk_combo_box_append_text(GTK_COMBO_BOX(combo), buf);

	/* Set last choice */
	gtk_combo_box_set_active(GTK_COMBO_BOX(combo), *num);

	/* Add combo box to action box */
	gtk_box_pack_end(GTK_BOX(action_box), combo, FALSE, TRUE, 0);

	/* Show everything */
	gtk_widget_show_all(combo);

	/* Process events */
	gtk_main();

	/* Get selection */
	i = gtk_combo_box_get_active(GTK_COMBO_BOX(combo));

	/* Destroy combo box */
	gtk_widget_destroy(combo);

	/* Check for last choice (no prevention) */
	if (i == *num)
	{
		/* Set no choice */
		*num = 0;
		return;
	}

	/* Select takeover to prevent */
	list[0] = list[i];
	*num = 1;
}

/*
 * Choose a world to upgrade.
 */
void gui_choose_upgrade(game *g, int who, int list[], int *num, int special[],
                        int *num_special)
{
	displayed *i_ptr;
	char buf[1024];
	int i, j, n = 0, ns = 0;

	/* Create prompt */
	sprintf(buf, "Choose world to replace");

	/* Set prompt */
	gtk_label_set_text(GTK_LABEL(action_prompt), buf);

	/* Reset displayed cards */
	reset_cards(g, FALSE, FALSE);

	/* Set button restriction */
	action_restrict = RESTRICT_UPGRADE;

	/* Activate action button */
	gtk_widget_set_sensitive(action_button, TRUE);

	/* Loop over cards in list */
	for (i = 0; i < *num; i++)
	{
		/* Loop over cards in hand */
		for (j = 0; j < hand_size; j++)
		{
			/* Get hand pointer */
			i_ptr = &hand[j];

			/* Check for matching index */
			if (i_ptr->index == list[i])
			{
				/* Card is eligible */
				i_ptr->eligible = 1;
			}
		}
	}

	/* Loop over special cards */
	for (i = 0; i < *num_special; i++)
	{
		/* Loop over cards on table */
		for (j = 0; j < table_size[player_us]; j++)
		{
			/* Get table card pointer */
			i_ptr = &table[player_us][j];

			/* Check for matching index */
			if (i_ptr->index == special[i])
			{
				/* Card is eligible */
				i_ptr->eligible = 1;

				/* Card should be highlighted when selected */
				i_ptr->highlight = 1;
			}
		}
	}

	/* Redraw everything */
	redraw_everything();

	/* Process events */
	gtk_main();

	/* Loop over cards in hand */
	for (i = 0; i < hand_size; i++)
	{
		/* Get hand pointer */
		i_ptr = &hand[i];

		/* Check for selected */
		if (i_ptr->selected)
		{
			/* Add to list */
			list[n++] = i_ptr->index;
		}
	}

	/* Set number of cards selected */
	*num = n;

	/* Loop over cards on table */
	for (i = 0; i < table_size[player_us]; i++)
	{
		/* Get table card pointer */
		i_ptr = &table[player_us][i];

		/* Check for selected */
		if (i_ptr->selected)
		{
			/* Add to list */
			special[ns++] = i_ptr->index;
		}
	}

	/* Set number of special cards selected */
	*num_special = ns;
}

/*
 * Choose a good to trade.
 */
void gui_choose_trade(game *g, int who, int list[], int *num, int no_bonus)
{
	char buf[1024];
	displayed *i_ptr;
	int i, j;

	/* Create prompt */
	sprintf(buf, "Choose good to trade%s", no_bonus ? " (no bonuses)" : "");

	/* Set prompt */
	gtk_label_set_text(GTK_LABEL(action_prompt), buf);

	/* Set restrictions on action button */
	action_restrict = RESTRICT_NUM;
	action_min = action_max = 1;

	/* Deactivate action button */
	gtk_widget_set_sensitive(action_button, FALSE);

	/* Reset displayed cards */
	reset_cards(g, TRUE, FALSE);

	/* Loop over cards in list */
	for (i = 0; i < *num; i++)
	{
		/* Loop over cards on table */
		for (j = 0; j < table_size[player_us]; j++)
		{
			/* Get displayed card pointer */
			i_ptr = &table[player_us][j];

			/* Check for matching index */
			if (i_ptr->index == list[i])
			{
				/* Card is eligible */
				i_ptr->eligible = 1;
			}
		}
	}

	/* Redraw everything */
	redraw_everything();

	/* Process events */
	gtk_main();

	/* Loop over cards on table */
	for (i = 0; i < table_size[player_us]; i++)
	{
		/* Get displayed card pointer */
		i_ptr = &table[player_us][i];

		/* Check for selected */
		if (i_ptr->selected)
		{
			/* Set choice */
			list[0] = i_ptr->index;
			*num = 1;

			/* Done */
			break;
		}
	}
}

/*
 * Store a power's location.
 */
typedef struct pow_loc
{
	/* Card index */
	int c_idx;

	/* Power index */
	int o_idx;

} pow_loc;

/*
 * Return a "score" for sorting consume powers.
 */
static int score_consume(power *o_ptr)
{
	int vp = 0, card = 0, goods = 1;
	int score;

	/* Always discard from hand last */
	if (o_ptr->code & P4_DISCARD_HAND) return 0;

	/* Check for free card draw */
	if (o_ptr->code & P4_DRAW) return o_ptr->value * 500;

	/* Check for free VP */
	if (o_ptr->code & P4_VP) return o_ptr->value * 1000;

	/* Check for VP awarded */
	if (o_ptr->code & P4_GET_VP) vp += o_ptr->value;

	/* Check for card awarded */
	if (o_ptr->code & P4_GET_CARD) card += o_ptr->value;

	/* Check for cards awarded */
	if (o_ptr->code & P4_GET_2_CARD) card += o_ptr->value * 2;

	/* Assume trade will earn 4 cards */
	if (o_ptr->code & P4_TRADE_ACTION) card += 4;

	/* Assume trade without bonus will earn fewer cards */
	if (o_ptr->code & P4_TRADE_NO_BONUS) card--;

	/* Check for consuming two goods */
	if (o_ptr->code & P4_CONSUME_TWO) goods = 2;

	/* Check for consuming three goods */
	if (o_ptr->code & P4_CONSUME_3_DIFF) goods = 3;

	/* Check for consuming all goods */
	if (o_ptr->code & P4_CONSUME_ALL) goods = 4;

	/* Check for "Consume x2" chosen */
	if (player_chose(&real_game, player_us, ACT_CONSUME_X2)) vp *= 2;

	/* Compute score */
	score = (vp * 100 + card * 50) / goods;

	/* Use specific consume powers first */
	if (!(o_ptr->code & P4_CONSUME_ANY)) score++;

	/* Use multi-use powers later */
	if (o_ptr->times > 1) score -= 5 * o_ptr->times;

	/* Return score */
	return score;
}

/*
 * Compare two consume powers for sorting.
 */
static int cmp_consume(const void *l1, const void *l2)
{
	pow_loc *l_ptr1 = (pow_loc *)l1;
	pow_loc *l_ptr2 = (pow_loc *)l2;
	power *o_ptr1;
	power *o_ptr2;

	/* Get first power */
	o_ptr1 = &real_game.deck[l_ptr1->c_idx].d_ptr->powers[l_ptr1->o_idx];
	o_ptr2 = &real_game.deck[l_ptr2->c_idx].d_ptr->powers[l_ptr2->o_idx];

	/* Compare consume powers */
	return score_consume(o_ptr2) - score_consume(o_ptr1);
}

/*
 * Ask user which consume power to use.
 */
void gui_choose_consume(game *g, int who, int cidx[], int oidx[], int *num,
                        int optional)
{
	GtkWidget *combo;
	card *c_ptr;
	power *o_ptr;
	pow_loc l_list[MAX_DECK];
	char buf[1024], *name, buf2[1024];
	int i;

	/* Activate action button */
	gtk_widget_set_sensitive(action_button, TRUE);

	/* Reset displayed cards */
	reset_cards(g, TRUE, TRUE);

	/* Redraw everything */
	redraw_everything();

	/* Set prompt */
	gtk_label_set_text(GTK_LABEL(action_prompt), "Choose Consume power");

	/* Create simple combo box */
	combo = gtk_combo_box_new_text();

	/* Loop over powers */
	for (i = 0; i < *num; i++)
	{
		/* Create power location */
		l_list[i].c_idx = cidx[i];
		l_list[i].o_idx = oidx[i];
	}

	/* Sort consume powers */
	qsort(l_list, *num, sizeof(pow_loc), cmp_consume);

	/* Loop over powers */
	for (i = 0; i < *num; i++)
	{
		/* Get card pointer */
		c_ptr = &g->deck[l_list[i].c_idx];

		/* Get power pointer */
		o_ptr = &c_ptr->d_ptr->powers[l_list[i].o_idx];

		/* Check for simple powers */
		if (o_ptr->code == P4_DRAW)
		{
			/* Make string */
			sprintf(buf, "Draw %d", o_ptr->value);
		}
		else if (o_ptr->code == P4_VP)
		{
			/* Make string */
			sprintf(buf, "Take VP");
		}
		else if (o_ptr->code == P4_DRAW_LUCKY)
		{
			/* Make string */
			sprintf(buf, "Draw if lucky");
		}
		else if (o_ptr->code == P4_ANTE_CARD)
		{
			/* Make string */
			sprintf(buf, "Ante card");
		}
		else if (o_ptr->code & P4_CONSUME_3_DIFF)
		{
			/* Make string */
			sprintf(buf, "Consume 3 kinds");
		}
		else if (o_ptr->code & P4_CONSUME_N_DIFF)
		{
			/* Make string */
			sprintf(buf, "Consume different kinds");
		}
		else if (o_ptr->code & P4_CONSUME_ALL)
		{
			/* Make string */
			sprintf(buf, "Consume all goods");
		}
		else if (o_ptr->code & P4_TRADE_ACTION)
		{
			/* Make string */
			sprintf(buf, "Trade good");

			/* Check for no bonuses */
			if (o_ptr->code & P4_TRADE_NO_BONUS)
			{
				/* Append qualifier */
				strcat(buf, " (no bonus)");
			}
		}
		else
		{
			/* Get type of good to consume */
			if (o_ptr->code & P4_CONSUME_NOVELTY)
			{
				/* Novelty good */
				name = "Novelty ";
			}
			else if (o_ptr->code & P4_CONSUME_RARE)
			{
				/* Rare good */
				name = "Rare ";
			}
			else if (o_ptr->code & P4_CONSUME_GENE)
			{
				/* Gene good */
				name = "Gene ";
			}
			else if (o_ptr->code & P4_CONSUME_ALIEN)
			{
				/* Alien good */
				name = "Alien ";
			}
			else
			{
				/* Any good */
				name = "";
			}

			/* Start consume string */
			if (o_ptr->code & P4_DISCARD_HAND)
			{
				/* Make string */
				sprintf(buf, "Consume from hand for ");
			}
			else if (o_ptr->code & P4_CONSUME_TWO)
			{
				/* Start string */
				sprintf(buf, "Consume two %sgoods for ", name);
			}
			else if (o_ptr->code & P4_CONSUME_PRESTIGE)
			{
				/* Make string */
				sprintf(buf, "Consume prestige for ");
			}
			else
			{
				/* Start string */
				sprintf(buf, "Consume %sgood for ", name);
			}

			/* Check for cards */
			if (o_ptr->code & P4_GET_CARD)
			{
				/* Create card reward string */
				sprintf(buf2, "%d card%s", o_ptr->value,
				        (o_ptr->value != 1) ? "s" : "");

				/* Add to string */
				strcat(buf, buf2);

				/* Check for other reward as well */
				if (o_ptr->code & (P4_GET_VP | P4_GET_PRESTIGE))
				{
					/* Add "and" */
					strcat(buf, " and ");
				}
			}

			/* Check for extra cards */
			if (o_ptr->code & P4_GET_2_CARD)
			{
				/* Create card reward string */
				strcat(buf, "2 cards");

				/* Check for other reward as well */
				if (o_ptr->code & (P4_GET_VP | P4_GET_PRESTIGE))
				{
					/* Add "and" */
					strcat(buf, " and ");
				}
			}

			/* Check for points */
			if (o_ptr->code & P4_GET_VP)
			{
				/* Create VP reward string */
				sprintf(buf2, "%d VP", o_ptr->value);

				/* Add to string */
				strcat(buf, buf2);

				/* Check for other reward as well */
				if (o_ptr->code & P4_GET_PRESTIGE)
				{
					/* Add "and" */
					strcat(buf, " and ");
				}
			}

			/* Check for prestige */
			if (o_ptr->code & P4_GET_PRESTIGE)
			{
				/* Create prestige reward string */
				sprintf(buf2, "%d prestige", o_ptr->value);

				/* Add to string */
				strcat(buf, buf2);
			}

			/* Check for multiple times */
			if (o_ptr->times > 1)
			{
				/* Create times string */
				sprintf(buf2, " (x%d)", o_ptr->times);

				/* Add to string */
				strcat(buf, buf2);
			}
		}

		/* Append option to combo box */
		gtk_combo_box_append_text(GTK_COMBO_BOX(combo), buf);
	}

	/* Check for all optional powers */
	if (optional)
	{
		/* Append no choice option */
		sprintf(buf, "None (done with Consume)");

		/* Append option to combo box */
		gtk_combo_box_append_text(GTK_COMBO_BOX(combo), buf);
	}

	/* Set first choice */
	gtk_combo_box_set_active(GTK_COMBO_BOX(combo), 0);

	/* Add combo box to action box */
	gtk_box_pack_end(GTK_BOX(action_box), combo, FALSE, TRUE, 0);

	/* Show everything */
	gtk_widget_show_all(combo);

	/* Process events */
	gtk_main();

	/* Get selection */
	i = gtk_combo_box_get_active(GTK_COMBO_BOX(combo));

	/* Destroy combo box */
	gtk_widget_destroy(combo);

	/* Check for done */
	if (i == *num)
	{
		/* Set no choice */
		*num = 0;
		return;
	}

	/* Select chosen power */
	cidx[0] = l_list[i].c_idx;
	oidx[0] = l_list[i].o_idx;
	*num = 1;
}

/*
 * Consume cards from hand.
 */
void gui_choose_consume_hand(game *g, int who, int c_idx, int o_idx, int list[],
                             int *num)
{
	card *c_ptr;
	power *o_ptr;
	char buf[1024];
	displayed *i_ptr;
	int i, j, n = 0;

	/* Get card pointer */
	c_ptr = &g->deck[c_idx];

	/* Get power pointer */
	o_ptr = &c_ptr->d_ptr->powers[o_idx];

	/* Check for needing two cards */
	if (o_ptr->code & P4_CONSUME_TWO)
	{
		/* Create prompt */
		sprintf(buf, "Choose cards to consume on %s",
		        c_ptr->d_ptr->name);
	}
	else
	{
		/* Create prompt */
		sprintf(buf, "Choose up to %d cards to consume on %s",
		        o_ptr->times, c_ptr->d_ptr->name);
	}

	/* Set prompt */
	gtk_label_set_text(GTK_LABEL(action_prompt), buf);

	/* Set restrictions on action button */
	action_restrict = RESTRICT_CONSUME;
	action_cidx = c_idx;
	action_oidx = o_idx;

	/* Activate action button */
	gtk_widget_set_sensitive(action_button, TRUE);

	/* Reset displayed cards */
	reset_cards(g, FALSE, TRUE);

	/* Loop over cards in list */
	for (i = 0; i < *num; i++)
	{
		/* Loop over cards in hand */
		for (j = 0; j < hand_size; j++)
		{
			/* Get hand pointer */
			i_ptr = &hand[j];

			/* Check for matching index */
			if (i_ptr->index == list[i])
			{
				/* Card is eligible */
				i_ptr->eligible = 1;
			}
		}
	}

	/* Redraw everything */
	redraw_everything();

	/* Process events */
	gtk_main();

	/* Loop over cards in hand */
	for (i = 0; i < hand_size; i++)
	{
		/* Get hand pointer */
		i_ptr = &hand[i];

		/* Check for selected */
		if (i_ptr->selected)
		{
			/* Add to list */
			list[n++] = i_ptr->index;
		}
	}

	/* Set number of cards selected */
	*num = n;
}

/*
 * Choose good(s) to consume.
 */
void gui_choose_good(game *g, int who, int c_idx, int o_idx, int goods[],
                     int *num, int min, int max)
{
	card *c_ptr;
	char buf[1024];
	displayed *i_ptr;
	int i, j, n = 0;

	/* Get pointer to card holding consume power */
	c_ptr = &real_game.deck[c_idx];

	/* Create prompt */
	sprintf(buf, "Choose goods to consume on %s", c_ptr->d_ptr->name);

	/* Set prompt */
	gtk_label_set_text(GTK_LABEL(action_prompt), buf);

	/* Set restrictions on action button */
	action_restrict = RESTRICT_GOOD;
	action_min = min;
	action_max = max;
	action_cidx = c_idx;
	action_oidx = o_idx;

	/* Deactivate action button */
	gtk_widget_set_sensitive(action_button, min == 0);

	/* Reset displayed cards */
	reset_cards(g, TRUE, FALSE);

	/* Loop over cards in list */
	for (i = 0; i < *num; i++)
	{
		/* Loop over cards on table */
		for (j = 0; j < table_size[player_us]; j++)
		{
			/* Get displayed card pointer */
			i_ptr = &table[player_us][j];

			/* Check for matching index */
			if (i_ptr->index == goods[i])
			{
				/* Card is eligible */
				i_ptr->eligible = 1;
			}
		}
	}

	/* Redraw everything */
	redraw_everything();

	/* Process events */
	gtk_main();

	/* Loop over cards on table */
	for (i = 0; i < table_size[player_us]; i++)
	{
		/* Get displayed card pointer */
		i_ptr = &table[player_us][i];

		/* Check for selected */
		if (i_ptr->selected)
		{
			/* Add to list */
			goods[n++] = i_ptr->index;
		}
	}

	/* Set number of goods chosen */
	*num = n;
}

/*
 * Choose a number from 1-7.
 */
int gui_choose_lucky(game *g, int who)
{
	GtkWidget *spin;
	int i;

	/* Activate action button */
	gtk_widget_set_sensitive(action_button, TRUE);

	/* Reset displayed cards */
	reset_cards(g, TRUE, TRUE);

	/* Redraw everything */
	redraw_everything();

	/* Set prompt */
	gtk_label_set_text(GTK_LABEL(action_prompt), "Choose Number");

	/* Create simple spin button */
	spin = gtk_spin_button_new_with_range(1, 7, 1);

	/* Add spin button to action box */
	gtk_box_pack_end(GTK_BOX(action_box), spin, FALSE, TRUE, 0);

	/* Show everything */
	gtk_widget_show_all(spin);

	/* Process events */
	gtk_main();

	/* Get selection */
	i = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(spin));

	/* Destroy spin button */
	gtk_widget_destroy(spin);

	/* Return choice */
	return i;
}

/*
 * Choose card to ante.
 */
int gui_choose_ante(game *g, int who, int list[], int num)
{
	char buf[1024];
	displayed *i_ptr;
	int i, j;

	/* Create prompt */
	sprintf(buf, "Choose card to ante");

	/* Set prompt */
	gtk_label_set_text(GTK_LABEL(action_prompt), buf);

	/* Set restrictions on action button */
	action_restrict = RESTRICT_NUM;
	action_min = 0;
	action_max = 1;

	/* Activate action button */
	gtk_widget_set_sensitive(action_button, TRUE);

	/* Reset displayed cards */
	reset_cards(g, FALSE, TRUE);

	/* Loop over cards in list */
	for (i = 0; i < num; i++)
	{
		/* Loop over cards in hand */
		for (j = 0; j < hand_size; j++)
		{
			/* Get hand pointer */
			i_ptr = &hand[j];

			/* Check for matching index */
			if (i_ptr->index == list[i])
			{
				/* Card is eligible */
				i_ptr->eligible = 1;
			}
		}
	}

	/* Redraw everything */
	redraw_everything();

	/* Process events */
	gtk_main();

	/* Loop over cards in hand */
	for (i = 0; i < hand_size; i++)
	{
		/* Get hand pointer */
		i_ptr = &hand[i];

		/* Check for selected */
		if (i_ptr->selected)
		{
			/* Return selected card */
			return i_ptr->index;
		}
	}

	/* No card selected */
	return -1;
}

/*
 * Choose a card to keep from a successful gamble.
 */
int gui_choose_keep(game *g, int who, int list[], int num)
{
	card *c_ptr;
	displayed *i_ptr;
	char buf[1024];
	int i;

	/* Check for only one choice */
	if (num == 1) return list[0];

	/* Create prompt */
	sprintf(buf, "Choose card to keep");

	/* Set prompt */
	gtk_label_set_text(GTK_LABEL(action_prompt), buf);

	/* Reset displayed cards */
	reset_cards(g, FALSE, TRUE);

	/* Set button restriction */
	action_restrict = RESTRICT_NUM;
	action_min = action_max = 1;

	/* Deactivate action button */
	gtk_widget_set_sensitive(action_button, FALSE);

	/* Add cards to "hand" */
	for (i = 0; i < num; i++)
	{
		/* Get card pointer */
		c_ptr = &real_game.deck[list[i]];

		/* Get next entry in hand list */
		i_ptr = &hand[hand_size++];

		/* Add card information */
		i_ptr->index = list[i];
		i_ptr->d_ptr = c_ptr->d_ptr;

		/* Card is in hand */
		i_ptr->hand = 1;

		/* Card is eligible */
		i_ptr->eligible = 1;
		i_ptr->gapped = 1;
		i_ptr->highlight = 1;
		
		/* Card is not selected */
		i_ptr->selected = 0;
	}

	/* Redraw everything */
	redraw_everything();

	/* Process events */
	gtk_main();

	/* Loop over cards in hand */
	for (i = 0; i < hand_size; i++)
	{
		/* Get hand pointer */
		i_ptr = &hand[i];

		/* Check for selected */
		if (i_ptr->selected)
		{
			/* Return choice */
			return i_ptr->index;
		}
	}

	/* Error */
	return -1;
}

/*
 * Choose a windfall world to produce on.
 */
void gui_choose_windfall(game *g, int who, int list[], int *num)
{
	char buf[1024];
	displayed *i_ptr;
	int i, j;

	/* Create prompt */
	sprintf(buf, "Choose windfall world to produce");

	/* Set prompt */
	gtk_label_set_text(GTK_LABEL(action_prompt), buf);

	/* Set restrictions on action button */
	action_restrict = RESTRICT_NUM;
	action_min = action_max = 1;

	/* Deactivate action button */
	gtk_widget_set_sensitive(action_button, FALSE);

	/* Reset displayed cards */
	reset_cards(g, TRUE, FALSE);

	/* Loop over cards in list */
	for (i = 0; i < *num; i++)
	{
		/* Loop over cards on table */
		for (j = 0; j < table_size[player_us]; j++)
		{
			/* Get displayed card pointer */
			i_ptr = &table[player_us][j];

			/* Check for matching index */
			if (i_ptr->index == list[i])
			{
				/* Card is eligible */
				i_ptr->eligible = 1;
			}
		}
	}

	/* Redraw everything */
	redraw_everything();

	/* Process events */
	gtk_main();

	/* Loop over cards on table */
	for (i = 0; i < table_size[player_us]; i++)
	{
		/* Get displayed card pointer */
		i_ptr = &table[player_us][i];

		/* Check for selected */
		if (i_ptr->selected)
		{
			/* Set choice */
			list[0] = i_ptr->index;
			*num = 1;
		}
	}
}

/*
 * Choose a produce power to use.
 */
void gui_choose_produce(game *g, int who, int cidx[], int oidx[], int num)
{
	GtkWidget *combo;
	card *c_ptr = NULL;
	power *o_ptr, bonus;
	char buf[1024];
	int i;

	/* Activate action button */
	gtk_widget_set_sensitive(action_button, TRUE);

	/* Reset displayed cards */
	reset_cards(g, TRUE, TRUE);

	/* Redraw everything */
	redraw_everything();

	/* Set prompt */
	gtk_label_set_text(GTK_LABEL(action_prompt), "Choose Produce power");

	/* Create simple combo box */
	combo = gtk_combo_box_new_text();

	/* Loop over powers */
	for (i = 0; i < num; i++)
	{
		/* Check for produce or prestige bonus */
		if (cidx[i] < 0)
		{
			/* Create fake produce power */
			bonus.code = P5_WINDFALL_ANY;
			o_ptr = &bonus;
		}
		else
		{
			/* Get card pointer */
			c_ptr = &g->deck[cidx[i]];

			/* Get power pointer */
			o_ptr = &c_ptr->d_ptr->powers[oidx[i]];
		}

		/* Clear string describing power */
		strcpy(buf, "");

		/* Check for simple powers */
		if (o_ptr->code & P5_DRAW_EACH_NOVELTY)
		{
			/* Make string */
			sprintf(buf, "Draw per Novelty produced");
		}
		else if (o_ptr->code & P5_DRAW_EACH_RARE)
		{
			/* Make string */
			sprintf(buf, "Draw per Rare produced");
		}
		else if (o_ptr->code & P5_DRAW_EACH_GENE)
		{
			/* Make string */
			sprintf(buf, "Draw per Gene produced");
		}
		else if (o_ptr->code & P5_DRAW_EACH_ALIEN)
		{
			/* Make string */
			sprintf(buf, "Draw per Alien produced");
		}
		else if (o_ptr->code & P5_DRAW_DIFFERENT)
		{
			/* Make string */
			sprintf(buf, "Draw per type produced");
		}

		/* Check for discard required */
		if (o_ptr->code & P5_DISCARD)
		{
			/* Start string */
			sprintf(buf, "Discard to ");
		}

		/* Regular production powers */
		if (o_ptr->code & P5_PRODUCE)
		{
			/* Add to string */
			strcat(buf, "produce on ");
			strcat(buf, c_ptr->d_ptr->name);
		}
		else if (o_ptr->code & P5_WINDFALL_ANY)
		{
			/* Add to string */
			strcat(buf, "produce on any windfall");
		}
		else if (o_ptr->code & P5_WINDFALL_NOVELTY)
		{
			/* Add to string */
			strcat(buf, "produce on Novelty windfall");
		}
		else if (o_ptr->code & P5_WINDFALL_RARE)
		{
			/* Add to string */
			strcat(buf, "produce on Rare windfall");
		}
		else if (o_ptr->code & P5_WINDFALL_GENE)
		{
			/* Add to string */
			strcat(buf, "produce on Genes windfall");
		}
		else if (o_ptr->code & P5_WINDFALL_ALIEN)
		{
			/* Add to string */
			strcat(buf, "produce on Alien windfall");
		}

		/* Capitalize string if needed */
		buf[0] = toupper(buf[0]);

		/* Append option to combo box */
		gtk_combo_box_append_text(GTK_COMBO_BOX(combo), buf);
	}

	/* Set first choice */
	gtk_combo_box_set_active(GTK_COMBO_BOX(combo), 0);

	/* Add combo box to action box */
	gtk_box_pack_end(GTK_BOX(action_box), combo, FALSE, TRUE, 0);

	/* Show everything */
	gtk_widget_show_all(combo);

	/* Process events */
	gtk_main();

	/* Get selection */
	i = gtk_combo_box_get_active(GTK_COMBO_BOX(combo));

	/* Destroy combo box */
	gtk_widget_destroy(combo);

	/* Select chosen power */
	cidx[0] = cidx[i];
	oidx[0] = oidx[i];
}

/*
 * Discard a card in order to produce.
 */
void gui_choose_discard_produce(game *g, int who, int list[], int *num,
                                int special[], int *num_special)
{
	char buf[1024];
	displayed *i_ptr;
	int i, j;

	/* Create prompt */
	sprintf(buf, "Choose discard to produce");

	/* Set prompt */
	gtk_label_set_text(GTK_LABEL(action_prompt), buf);

	/* Set restrictions on action button */
	action_restrict = RESTRICT_BOTH;
	action_min = 0;
	action_max = 1;

	/* Activate action button */
	gtk_widget_set_sensitive(action_button, TRUE);

	/* Reset displayed cards */
	reset_cards(g, FALSE, FALSE);

	/* Loop over cards in list */
	for (i = 0; i < *num; i++)
	{
		/* Loop over cards in hand */
		for (j = 0; j < hand_size; j++)
		{
			/* Get hand pointer */
			i_ptr = &hand[j];

			/* Check for matching index */
			if (i_ptr->index == list[i])
			{
				/* Card is eligible */
				i_ptr->eligible = 1;
			}
		}
	}

	/* Loop over special cards */
	for (i = 0; i < *num_special; i++)
	{
		/* Loop over cards on table */
		for (j = 0; j < table_size[player_us]; j++)
		{
			/* Get table card pointer */
			i_ptr = &table[player_us][j];

			/* Check for matching index */
			if (i_ptr->index == special[i])
			{
				/* Card is eligible */
				i_ptr->eligible = 1;

				/* Card should be highlighted when selected */
				i_ptr->highlight = 1;

				/* Check for only choice */
				if (*num_special == 1)
				{
					/* Start with card selected */
					i_ptr->selected = 1;
				}
			}
		}
	}

	/* Redraw everything */
	redraw_everything();

	/* Process events */
	gtk_main();

	/* Assume no choice made */
	*num = *num_special = 0;

	/* Loop over cards in hand */
	for (i = 0; i < hand_size; i++)
	{
		/* Get hand pointer */
		i_ptr = &hand[i];

		/* Check for selected */
		if (i_ptr->selected)
		{
			/* Set choice */
			list[0] = i_ptr->index;
			*num = 1;
		}
	}

	/* Loop over cards on table */
	for (i = 0; i < table_size[player_us]; i++)
	{
		/* Get displayed card pointer */
		i_ptr = &table[player_us][i];

		/* Check for selected */
		if (i_ptr->selected)
		{
			/* Set choice */
			special[0] = i_ptr->index;
			*num_special = 1;
		}
	}

	/* Check for only one or the other choice made */
	if (!(*num) || !(*num_special))
	{
		/* Clear selection */
		*num = *num_special = 0;
	}
}

/*
 * Choose a search category.
 */
int gui_choose_search_type(game *g, int who)
{
	GtkWidget *combo;
	char buf[1024];
	int i;

	/* Activate action button */
	gtk_widget_set_sensitive(action_button, TRUE);

	/* Reset displayed cards */
	reset_cards(g, TRUE, TRUE);

	/* Redraw everything */
	redraw_everything();

	/* Set prompt */
	gtk_label_set_text(GTK_LABEL(action_prompt), "Choose Search category");

	/* Create simple combo box */
	combo = gtk_combo_box_new_text();

	/* Loop over search categories */
	for (i = 0; i < MAX_SEARCH; i++)
	{
		/* Skip takeover category if disabled */
		if (real_game.takeover_disabled && i == SEARCH_TAKEOVER)
			continue;

		/* Copy search name */
		strcpy(buf, search_name[i]);

		/* Capitalize search name */
		buf[0] = toupper(buf[0]);

		/* Append option to combo box */
		gtk_combo_box_append_text(GTK_COMBO_BOX(combo), buf);
	}

	/* Set first choice */
	gtk_combo_box_set_active(GTK_COMBO_BOX(combo), 0);

	/* Add combo box to action box */
	gtk_box_pack_end(GTK_BOX(action_box), combo, FALSE, TRUE, 0);

	/* Show everything */
	gtk_widget_show_all(combo);

	/* Process events */
	gtk_main();

	/* Get selection */
	i = gtk_combo_box_get_active(GTK_COMBO_BOX(combo));

	/* Destroy combo box */
	gtk_widget_destroy(combo);

	/* Return choice */
	return i;
}

/*
 * Ask player to keep or decline card found in search.
 */
int gui_choose_search_keep(game *g, int who, int arg1, int arg2)
{
	card *c_ptr;
	displayed *i_ptr;
	char buf[1024];

	/* Get card pointer */
	c_ptr = &real_game.deck[arg1];

	/* Create prompt */
	sprintf(buf, "Choose to keep/discard %s", c_ptr->d_ptr->name);

	/* Set prompt */
	gtk_label_set_text(GTK_LABEL(action_prompt), buf);

	/* Reset displayed cards */
	reset_cards(g, FALSE, TRUE);

	/* Set button restriction */
	action_restrict = RESTRICT_NUM;
	action_min = 0;
	action_max = 1;

	/* Activate action button */
	gtk_widget_set_sensitive(action_button, TRUE);

	/* Get next entry in hand list */
	i_ptr = &hand[hand_size++];

	/* Add card information */
	i_ptr->index = arg1;
	i_ptr->d_ptr = c_ptr->d_ptr;

	/* Card is in hand */
	i_ptr->hand = 1;

	/* Card is eligible */
	i_ptr->eligible = 1;
	i_ptr->gapped = 1;

	/* Card is not selected */
	i_ptr->selected = 0;

	/* Redraw everything */
	redraw_everything();

	/* Process events */
	gtk_main();

	/* Check for selected */
	return !i_ptr->selected;
}

/*
 * Player spots have been rotated.
 */
static void gui_notify_rotation(game *g, int who)
{
	GtkWidget *temp_area, *temp_status;
	int i;

	/* Remember our new player index */
	player_us--;

	/* Handle wraparound */
	if (player_us < 0) player_us = real_game.num_players - 1;

	/* Save first opponent area */
	temp_area = player_area[0];
	temp_status = player_status[0];

	/* Rotate opponent areas */
	for (i = 0; i < real_game.num_players - 1; i++)
	{
		/* Copy area and status */
		player_area[i] = player_area[i + 1];
		player_status[i] = player_status[i + 1];
	}

	/* Move first area to last spot */
	player_area[i] = temp_area;
	player_status[i] = temp_status;
}

/*
 * Make a choice of the given type.
 */
static void gui_make_choice(game *g, int who, int type, int list[], int *nl,
                           int special[], int *ns, int arg1, int arg2, int arg3)
{
	player *p_ptr;
	int i, rv;
	int *l_ptr;

	/* Determine type of choice */
	switch (type)
	{
		/* Action(s) to play */
		case CHOICE_ACTION:

			/* Choose actions */
			gui_choose_action(g, who, list, arg1);
			rv = 0;
			break;

		/* Start world */
		case CHOICE_START:

			/* Choose start world */
			gui_choose_start(g, who, list, nl, special, ns);
			rv = 0;
			break;

		/* Discard */
		case CHOICE_DISCARD:

			/* Choose discards */
			gui_choose_discard(g, who, list, nl, arg1);
			rv = 0;
			break;

		/* Save a card under a world for later */
		case CHOICE_SAVE:

			/* Choose card to save */
			gui_choose_save(g, who, list, nl);
			rv = 0;
			break;

		/* Choose to discard to gain prestige */
		case CHOICE_DISCARD_PRESTIGE:

			/* Choose card (if any) to discard */
			gui_choose_discard_prestige(g, who, list, nl);
			rv = 0;
			break;

		/* Place a development/world */
		case CHOICE_PLACE:

			/* Choose card to place */
			rv = gui_choose_place(g, who, list, *nl, arg1, arg2);
			break;

		/* Pay for a development/world */
		case CHOICE_PAYMENT:

			/* Choose payment */
			gui_choose_pay(g, who, arg1, list, nl, special, ns,
			               arg2);
			rv = 0;
			break;

		/* Choose a world to takeover */
		case CHOICE_TAKEOVER:

			/* Choose takeover target/power */
			rv = gui_choose_takeover(g, who, list, nl, special, ns);
			break;

		/* Choose a method of defense against a takeover */
		case CHOICE_DEFEND:

			/* Choose defense method */
			gui_choose_defend(g, who, arg1, arg2, arg3, list, nl,
			                  special, ns);
			rv = 0;
			break;

		/* Choose whether to prevent a takeover */
		case CHOICE_TAKEOVER_PREVENT:

			/* Choose takeover to prevent */
			gui_choose_takeover_prevent(g, who, list, nl, special);
			rv = 0;
			break;

		/* Choose world to upgrade with one from hand */
		case CHOICE_UPGRADE:

			/* Choose upgrade */
			gui_choose_upgrade(g, who, list, nl, special, ns);
			rv = 0;
			break;

		/* Choose a good to trade */
		case CHOICE_TRADE:

			/* Choose good */
			gui_choose_trade(g, who, list, nl, arg1);
			rv = 0;
			break;

		/* Choose a consume power to use */
		case CHOICE_CONSUME:

			/* Choose power */
			gui_choose_consume(g, who, list, special, nl, arg1);
			rv = 0;
			break;

		/* Choose discards from hand for VP */
		case CHOICE_CONSUME_HAND:

			/* Choose cards */
			gui_choose_consume_hand(g, who, arg1, arg2, list, nl);
			rv = 0;
			break;

		/* Choose good(s) to consume */
		case CHOICE_GOOD:

			/* Choose good(s) */
			gui_choose_good(g, who, special[0], special[1],
			                list, nl, arg1, arg2);
			rv = 0;
			break;

		/* Choose lucky number */
		case CHOICE_LUCKY:

			/* Choose number */
			rv = gui_choose_lucky(g, who);
			break;

		/* Choose card to ante */
		case CHOICE_ANTE:

			/* Choose card */
			rv = gui_choose_ante(g, who, list, *nl);
			break;

		/* Choose card to keep in successful gamble */
		case CHOICE_KEEP:

			/* Choose card */
			rv = gui_choose_keep(g, who, list, *nl);
			break;

		/* Choose windfall world to produce on */
		case CHOICE_WINDFALL:

			/* Choose world */
			gui_choose_windfall(g, who, list, nl);
			rv = 0;
			break;

		/* Choose produce power to use */
		case CHOICE_PRODUCE:

			/* Choose power */
			gui_choose_produce(g, who, list, special, *nl);
			rv = 0;
			break;

		/* Choose card to discard in order to produce */
		case CHOICE_DISCARD_PRODUCE:

			/* Choose card */
			gui_choose_discard_produce(g, who, list, nl,
			                           special, ns);
			rv = 0;
			break;

		/* Choose search category */
		case CHOICE_SEARCH_TYPE:

			/* Choose search type */
			rv = gui_choose_search_type(g, who);
			break;

		/* Choose whether to keep searched card */
		case CHOICE_SEARCH_KEEP:

			/* Choose to keep */
			rv = gui_choose_search_keep(g, who, arg1, arg2);
			break;

		/* Error */
		default:
			printf("Unknown choice type!\n");
			exit(1);
	}

	/* Get player pointer */
	p_ptr = &g->p[who];

	/* Get pointer to end of choice log */
	l_ptr = &p_ptr->choice_log[p_ptr->choice_size];

	/* Add choice type to log */
	*l_ptr++ = type;

	/* Add return value to log */
	*l_ptr++ = rv;

	/* Check for number of list items available */
	if (nl)
	{
		/* Add number of returned list items */
		*l_ptr++ = *nl;

		/* Copy list items */
		for (i = 0; i < *nl; i++)
		{
			/* Copy list item */
			*l_ptr++ = list[i];
		}
	}
	else
	{
		/* Add no list items */
		*l_ptr++ = 0;
	}

	/* Check for number of special items available */
	if (ns)
	{
		/* Add number of returned special items */
		*l_ptr++ = *ns;

		/* Copy special items */
		for (i = 0; i < *ns; i++)
		{
			/* Copy special item */
			*l_ptr++ = special[i];
		}
	}
	else
	{
		/* Add no special items */
		*l_ptr++ = 0;
	}

	/* Mark new size of choice log */
	p_ptr->choice_size = l_ptr - p_ptr->choice_log;
}

/*
 * Interface to GUI decision functions.
 */
decisions gui_func =
{
	NULL,
	gui_notify_rotation,
	gui_make_choice,
	gui_explore_sample,
	NULL,
	NULL
};

/*
 * Reset player structures.
 */
void reset_gui(void)
{
	int i;

	/* Reset our player index */
	player_us = 0;

	/* Reset choice logs */
	for (i = 0; i < MAX_PLAYER; i++)
	{
		/* Reset choice log size and position */
		real_game.p[i].choice_size = 0;
		real_game.p[i].choice_pos = 0;
	}

	/* Restore opponent areas to originial */
	for (i = 0; i < MAX_PLAYER; i++)
	{
		/* Restore table area */
		player_area[i] = orig_area[i];

		/* Restore status area */
		player_status[i] = orig_status[i];
	}

	/* Loop over all possible players */
	for (i = 0; i < MAX_PLAYER; i++)
	{
		/* Set name */
		real_game.p[i].name = player_names[i];
	}

	/* Restore player control functions */
	real_game.p[player_us].control = &gui_func;

	/* Loop over AI players */
	for (i = 1; i < MAX_PLAYER; i++)
	{
		/* Set control to AI functions */
		real_game.p[i].control = &ai_func;

		/* Call initialization function */
		real_game.p[i].control->init(&real_game, i, 0.0);
	}
}

/*
 * Modify GUI elements for the correct number of players.
 */
void modify_gui(void)
{
	int i;

	/* Check for basic game */
	if (!real_game.expanded || real_game.goal_disabled)
	{
		/* Hide goal area */
		gtk_widget_hide(goal_area);
	}
	else
	{
		/* Show goal area */
		gtk_widget_show(goal_area);
	}

	/* Loop over existing players */
	for (i = 0; i < real_game.num_players; i++)
	{
		/* Show status */
		gtk_widget_show_all(player_box[i]);
	}

	/* Loop over non-existant players */
	for ( ; i < MAX_PLAYER; i++)
	{
		/* Hide status */
		gtk_widget_hide_all(player_box[i]);
	}

	/* Show/hide separators */
	for (i = 1; i < MAX_PLAYER; i++)
	{
		/* Do not show first or last separators */
		if (i < 2 || i >= real_game.num_players)
		{
			/* Hide separator */
			gtk_widget_hide(player_sep[i]);
		}
		else
		{
			/* Show separator */
			gtk_widget_show(player_sep[i]);
		}
	}

	/* Check for no full-size image */
	if (opt.full_reduced == 2)
	{
		/* Hide image */
		gtk_widget_hide(full_image);
	}
	else
	{
		/* Show image */
		gtk_widget_show(full_image);
	}

	/* Redraw full-size image */
	redraw_full(NULL, NULL, NULL);

	/* Resize status areas */
	status_resize();

	/* Handle pending events */
	while (gtk_events_pending()) gtk_main_iteration();
}

/*
 * Run games forever.
 */
static void run_game(void)
{
	char buf[1024];

	/* Loop forever */
	while (1)
	{
		/* Check for new game starting */
		if (restart_loop == RESTART_NEW)
		{
			/* Reset game */
			reset_gui();

			/* Clear text log */
			clear_log();

			/* Disable undo/save menu item */
			gtk_widget_set_sensitive(undo_item, FALSE);
			gtk_widget_set_sensitive(save_item, FALSE);

			/* Initialize game */
			init_game(&real_game);

			/* Begin game */
			begin_game(&real_game);

			/* Enable undo/save menu item */
			gtk_widget_set_sensitive(undo_item, TRUE);
			gtk_widget_set_sensitive(save_item, TRUE);

			/* Check for aborted game */
			if (real_game.game_over) continue;

			/* Save beginning state */
			undo_state = real_game;
		}
		else if (restart_loop == RESTART_NONE)
		{
			/* Do nothing until disconnected from server */
			while (restart_loop == RESTART_NONE)
			{
				/* Wait for events */
				gtk_main();
			}

			/* Start a new game */
			restart_loop = RESTART_NEW;
			continue;
		}
		else
		{
			/* Reset game state */
			real_game = undo_state;
			save_state = undo_state;

			/* Check for undo request */
			if (restart_loop == RESTART_UNDO)
			{
				/* Add message */
				message_add(&real_game, "Turn undone.\n");
			}

			/* Check for load game request */
			else if (restart_loop == RESTART_LOAD)
			{
				/* Modify GUI for new game settings */
				modify_gui();

				/* Rotate players until we match */
				while (real_game.p[player_us].control !=
				       &gui_func)
				{
					/* Rotate player spots */
					gui_notify_rotation(&real_game, 0);
				}

				/* Clear message log */
				clear_log();
			}
		}

		/* Clear restart loop flag */
		restart_loop = 0;

		/* Play game rounds until finished */
		while (game_round(&real_game));

		/* Check for restart request */
		if (restart_loop)
		{
			/* Restart loop */
			continue;
		}

		/* Declare winner */
		declare_winner(&real_game);

		/* Reset displayed cards */
		reset_cards(&real_game, TRUE, TRUE);

		/* Redraw everything */
		redraw_everything();

		/* Create prompt */
		sprintf(buf, "Game Over");

		/* Set prompt */
		gtk_label_set_text(GTK_LABEL(action_prompt), buf);

		/* Process events */
		gtk_main();
	}
}

/*
 * Load user preferences file (if any).
 */
static void read_prefs(void)
{
	char *path;

    /* Build user preference filename */
#ifdef __APPLE__
	path = g_build_filename(g_get_home_dir(),
	                        "Library/Preferences/net.keldon.rftg", NULL);
#else
	path = g_build_filename(g_get_user_config_dir(), "rftg", NULL);
#endif

	/* Create keyfile structure */
	pref_file = g_key_file_new();

	/* Read config file */
	if (!g_key_file_load_from_file(pref_file, path,
	                               G_KEY_FILE_KEEP_COMMENTS, NULL))
	{
		/* No file to read */
		return;
	}

	/* Read game options */
	opt.num_players = g_key_file_get_integer(pref_file, "game",
	                                         "num_players", NULL);
	opt.expanded = g_key_file_get_integer(pref_file, "game", "expansion",
	                                      NULL);
	opt.advanced = g_key_file_get_boolean(pref_file, "game", "advanced",
	                                           NULL);
	opt.disable_goal = g_key_file_get_boolean(pref_file, "game", "no_goals",
	                                           NULL);
	opt.disable_takeover = g_key_file_get_boolean(pref_file, "game",
	                                              "no_takeover", NULL);

	/* Read GUI options */
	opt.full_reduced = g_key_file_get_integer(pref_file, "gui",
	                                          "full_reduced", NULL);
	opt.shrink_opponent = g_key_file_get_boolean(pref_file, "gui",
	                                             "shrink_opponent", NULL);

	/* Read multiplayer options */
	opt.server_name = g_key_file_get_string(pref_file, "multiplayer",
	                                        "server_name", NULL);
	opt.server_port = g_key_file_get_integer(pref_file, "multiplayer",
	                                         "server_port", NULL);
	opt.username = g_key_file_get_string(pref_file, "multiplayer",
	                                     "username", NULL);
	opt.password = g_key_file_get_string(pref_file, "multiplayer",
	                                     "password", NULL);

	/* Check range of values */
	if (opt.num_players < 2) opt.num_players = 2;
	if (opt.num_players > MAX_PLAYER) opt.num_players = MAX_PLAYER;
	if (opt.expanded < 0) opt.expanded = 0;
	if (opt.expanded > MAX_EXPANSION - 1) opt.expanded = MAX_EXPANSION - 1;
}

/*
 * Save preferences to file.
 */
void save_prefs(void)
{
	FILE *fff;
	char *path, *data;

	/* Build user preference filename */
#ifdef __APPLE__
	path = g_build_filename(g_get_home_dir(),
	                        "Library/Preferences/net.keldon.rftg", NULL);
#else
	path = g_build_filename(g_get_user_config_dir(), "rftg", NULL);
#endif

	/* Set game options */
	g_key_file_set_integer(pref_file, "game", "num_players",
	                       opt.num_players);
	g_key_file_set_integer(pref_file, "game", "expansion", opt.expanded);
	g_key_file_set_boolean(pref_file, "game", "advanced", opt.advanced);
	g_key_file_set_boolean(pref_file, "game", "no_goals", opt.disable_goal);
	g_key_file_set_boolean(pref_file, "game", "no_takeover",
	                       opt.disable_takeover);

	/* Set GUI options */
	g_key_file_set_integer(pref_file, "gui", "full_reduced",
	                       opt.full_reduced);
	g_key_file_set_boolean(pref_file, "gui", "shrink_opponent",
	                       opt.shrink_opponent);

	/* Set multiplayer options */
	g_key_file_set_string(pref_file, "multiplayer", "server_name",
	                      opt.server_name);
	g_key_file_set_integer(pref_file, "multiplayer", "server_port",
	                       opt.server_port);
	g_key_file_set_string(pref_file, "multiplayer", "username",
	                      opt.username);
	g_key_file_set_string(pref_file, "multiplayer", "password",
	                      opt.password);

	/* Open file for writing */
	fff = fopen(path, "w");

	/* Check for failure */
	if (!fff)
	{
		/* Error */
		printf("Can't save preferences to %s!\n", path);
		return;
	}

	/* Get contents of keyfile */
	data = g_key_file_to_data(pref_file, NULL, NULL);

	/* Write keyfile contents */
	fputs(data, fff);

	/* Free string */
	g_free(data);

	/* Close file */
	fclose(fff);
}

/*
 * Apply options to game structure.
 */
static void apply_options(void)
{
	/* Sanity check advanced mode */
	if (opt.num_players > 2)
	{
		/* Clear advanced mode */
		opt.advanced = 0;
	}

	/* Sanity check number of players in base game */
	if (opt.expanded < 1 && opt.num_players > 4)
	{
		/* Reset to four players */
		opt.num_players = 4;
	}

	/* Sanity check number of players in first expansion */
	if (opt.expanded < 2 && opt.num_players > 5)
	{
		/* Reset to five players */
		opt.num_players = 5;
	}

	/* Set number of players */
	real_game.num_players = opt.num_players;

	/* Set expansion level */
	real_game.expanded = opt.expanded;

	/* Set advanced flag */
	real_game.advanced = opt.advanced;

	/* Set goals disabled */
	real_game.goal_disabled = opt.disable_goal;

	/* Set takeover disabled */
	real_game.takeover_disabled = opt.disable_takeover;
}

/*
 * New game.
 */
static void gui_new_game(GtkMenuItem *menu_item, gpointer data)
{
	/* Check for connected to server */
	if (client_state != CS_DISCONN) return;

	/* Force game over */
	real_game.game_over = 1;
	
	/* Start new game immediately */
	restart_loop = RESTART_NEW;

	/* Quit waiting for events */
	gtk_main_quit();
}

/*
 * Load game.
 */
static void gui_load_game(GtkMenuItem *menu_item, gpointer data)
{
	GtkWidget *dialog;
	char *fname;
	int us, i;

	/* Check for connected to server */
	if (client_state != CS_DISCONN) return;

	/* Create file chooser dialog box */
	dialog = gtk_file_chooser_dialog_new("Load game", NULL,
	                                  GTK_FILE_CHOOSER_ACTION_OPEN,
	                                  GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
	                                  GTK_STOCK_OPEN, GTK_RESPONSE_ACCEPT,
                                          NULL);

	/* Run dialog and check response */
	if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT)
	{
		/* Get filename */
		fname = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));

		/* Try to load savefile into undo state */
		if (load_game(&undo_state, fname, &us) < 0)
		{
			/* Error */
		}

		/* Destroy filename */
		g_free(fname);

		/* Set our control functions */
		undo_state.p[us].control = &gui_func;

		/* Set all other control functions to AI */
		for (i = 0; i < undo_state.num_players; i++)
		{
			/* Skip our player */
			if (i == us) continue;

			/* Set to AI */
			undo_state.p[i].control = &ai_func;
		}

		/* Copy undo state to real game */
		real_game = undo_state;

		/* Reset game */
		reset_gui();

		/* Force current game over */
		real_game.game_over = 1;

		/* Switch to loaded state when able */
		restart_loop = RESTART_LOAD;

		/* Quit waiting for events */
		gtk_main_quit();
	}

	/* Destroy file choose dialog */
	gtk_widget_destroy(dialog);
}

/*
 * Save game.
 */
static void gui_save_game(GtkMenuItem *menu_item, gpointer data)
{
	GtkWidget *dialog;
	char *fname;

	/* Check for connected to server */
	if (client_state != CS_DISCONN) return;

	/* Create file chooser dialog box */
	dialog = gtk_file_chooser_dialog_new("Save game", NULL,
	                                  GTK_FILE_CHOOSER_ACTION_SAVE,
	                                  GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
	                                  GTK_STOCK_SAVE, GTK_RESPONSE_ACCEPT,
                                          NULL);

	/* Run dialog and check response */
	if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT)
	{
		/* Get filename */
		fname = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));

		/* Save to file */
		if (save_game(&save_state, fname, player_us) < 0)
		{
			/* Error */
		}

		/* Destroy filename */
		g_free(fname);
	}

	/* Destroy file chooser dialog */
	gtk_widget_destroy(dialog);
}

/*
 * Undo game.
 */
static void gui_undo_game(GtkMenuItem *menu_item, gpointer data)
{
	/* Check for connected to server */
	if (client_state != CS_DISCONN) return;

	/* Force game over */
	real_game.game_over = 1;
	
	/* Switch to undo state when able */
	restart_loop = RESTART_UNDO;

	/* Quit waiting for events */
	gtk_main_quit();
}

/*
 * Expansion level names.
 */
char *exp_names[] =
{
	"Base game only",
	"The Gathering Storm",
	"Rebel vs Imperium",
	"The Brink of War",
	NULL
};

/*
 * Labels for number of players.
 */
static char *player_labels[] =
{
	"Two players",
	"Three players",
	"Four players",
	"Five players",
	"Six players",
	NULL
};

/*
 * Full-size image option names.
 */
static char *reduce_names[] =
{
	"Original size",
	"Half size",
	"Hidden",
	NULL
};

/*
 * Checkbox widgets for select dialog.
 */
static GtkWidget *advanced_check;
static GtkWidget *disable_goal_check;
static GtkWidget *disable_takeover_check;

/*
 * Current selections for next game options.
 */
static int next_exp, next_player, next_reduce;

/*
 * React to an expansion level button being toggled.
 */
static void exp_toggle(GtkToggleButton *button, gpointer data)
{
	int i = GPOINTER_TO_INT(data);

	/* Check for button set */
	if (gtk_toggle_button_get_active(button))
	{
		/* Remember next expansion level */
		next_exp = i;

		/* Set goal disabled checkbox sensitivity */
		gtk_widget_set_sensitive(disable_goal_check, i > 0);

		/* Set takeover disabled checkbox sensitivity */
		gtk_widget_set_sensitive(disable_takeover_check, i > 1);
	}
}

/*
 * React to a player number button being toggled.
 */
static void player_toggle(GtkToggleButton *button, gpointer data)
{
	int i = GPOINTER_TO_INT(data);

	/* Check for button set */
	if (gtk_toggle_button_get_active(button))
	{
		/* Remember next game player number */
		next_player = i + 2;

		/* Set advanced game checkbox sensitivity */
		gtk_widget_set_sensitive(advanced_check, next_player == 2);
	}
}

/*
 * React to an full-size image option button being toggled.
 */
static void reduce_toggle(GtkToggleButton *button, gpointer data)
{
	int i = GPOINTER_TO_INT(data);

	/* Check for button set */
	if (gtk_toggle_button_get_active(button)) next_reduce = i;
}

/*
 * Select parameters and start a new game.
 */
static void select_parameters(GtkMenuItem *menu_item, gpointer data)
{
	GtkWidget *dialog;
	GtkWidget *radio = NULL;
	GtkWidget *exp_box, *player_box;
	GtkWidget *exp_frame, *player_frame;
	int i;

	/* Check for connected to server */
	if (client_state != CS_DISCONN) return;

	/* Create dialog box */
	dialog = gtk_dialog_new_with_buttons("Select Parameters", NULL,
	                                     GTK_DIALOG_MODAL,
	                                     GTK_STOCK_OK,
                                             GTK_RESPONSE_ACCEPT,
	                                     GTK_STOCK_CANCEL,
	                                     GTK_RESPONSE_REJECT, NULL);

	/* Set window title */
	gtk_window_set_title(GTK_WINDOW(dialog),
	                     "Race for the Galaxy " VERSION);

	/* Create vbox to hold expansion selection radio buttons */
	exp_box = gtk_vbox_new(FALSE, 0);

	/* Loop over expansion levels */
	for (i = 0; exp_names[i]; i++)
	{
		/* Create radio button */
		radio = gtk_radio_button_new_with_label_from_widget(
		                                        GTK_RADIO_BUTTON(radio),
		                                        exp_names[i]);

		/* Check for current expansion level */
		if (real_game.expanded == i)
		{
			/* Set button active */
			gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(radio),
			                             TRUE);

			/* Remember current expansion */
			next_exp = i;
		}

		/* Add handler */
		g_signal_connect(G_OBJECT(radio), "toggled",
		                 G_CALLBACK(exp_toggle), GINT_TO_POINTER(i));

		/* Pack radio button into box */
		gtk_box_pack_start(GTK_BOX(exp_box), radio, FALSE, TRUE, 0);
	}

	/* Create frame around buttons */
	exp_frame = gtk_frame_new("Choose expansion level");

	/* Pack radio button box into frame */
	gtk_container_add(GTK_CONTAINER(exp_frame), exp_box);

	/* Add frame to dialog box */
	gtk_container_add(GTK_CONTAINER(GTK_DIALOG(dialog)->vbox), exp_frame);

	/* Create vbox to hold player selection radio buttons */
	player_box = gtk_vbox_new(FALSE, 0);

	/* Clear current radio button widget */
	radio = NULL;

	/* Loop over expansion levels */
	for (i = 0; player_labels[i]; i++)
	{
		/* Create radio button */
		radio = gtk_radio_button_new_with_label_from_widget(
		                                        GTK_RADIO_BUTTON(radio),
		                                        player_labels[i]);

		/* Check for current number of players */
		if (real_game.num_players == i + 2)
		{
			/* Set button active */
			gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(radio),
			                             TRUE);

			/* Remember current expansion */
			next_player = i + 2;
		}

		/* Add handler */
		g_signal_connect(G_OBJECT(radio), "toggled",
		                 G_CALLBACK(player_toggle), GINT_TO_POINTER(i));

		/* Pack radio button into box */
		gtk_box_pack_start(GTK_BOX(player_box), radio, FALSE, TRUE, 0);
	}

	/* Create frame around buttons */
	player_frame = gtk_frame_new("Choose number of players");

	/* Pack radio button box into frame */
	gtk_container_add(GTK_CONTAINER(player_frame), player_box);

	/* Add frame to dialog box */
	gtk_container_add(GTK_CONTAINER(GTK_DIALOG(dialog)->vbox),player_frame);

	/* Create check box for two-player advanced game */
	advanced_check = gtk_check_button_new_with_label("Two-player advanced");

	/* Set checkbox status */
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(advanced_check),
	                             opt.advanced);

	/* Add checkbox to dialog box */
	gtk_container_add(GTK_CONTAINER(GTK_DIALOG(dialog)->vbox),
	                  advanced_check);

	/* Disable advanced checkbox if not two-player game */
	if (real_game.num_players != 2)
	{
		/* Disable checkbox */
		gtk_widget_set_sensitive(advanced_check, FALSE);
	}

	/* Create check box for disabled goals */
	disable_goal_check = gtk_check_button_new_with_label("Disable goals");

	/* Set checkbox status */
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(disable_goal_check),
	                             opt.disable_goal);

	/* Add checkbox to dialog box */
	gtk_container_add(GTK_CONTAINER(GTK_DIALOG(dialog)->vbox),
	                  disable_goal_check);

	/* Disable goal checkbox if not expanded game */
	if (opt.expanded < 1)
	{
		/* Disable checkbox */
		gtk_widget_set_sensitive(disable_goal_check, FALSE);
	}

	/* Create check box for disabled takeovers */
	disable_takeover_check =
	                   gtk_check_button_new_with_label("Disable takeovers");

	/* Set checkbox status */
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(disable_takeover_check),
	                             opt.disable_takeover);

	/* Add checkbox to dialog box */
	gtk_container_add(GTK_CONTAINER(GTK_DIALOG(dialog)->vbox),
	                  disable_takeover_check);

	/* Disable takeover checkbox if not expanded game */
	if (opt.expanded < 2)
	{
		/* Disable checkbox */
		gtk_widget_set_sensitive(disable_takeover_check, FALSE);
	}

	/* Show all widgets */
	gtk_widget_show_all(dialog);

	/* Run dialog */
	if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT)
	{
		/* Check for too many players */
		if (next_exp == 0 && next_player > 4) next_player = 4;
		if (next_exp == 1 && next_player > 5) next_player = 5;

		/* Set expansion level */
		opt.expanded = next_exp;

		/* Set number of players */
		opt.num_players = next_player;

		/* Set advanced game flag */
		opt.advanced = (next_player == 2) &&
		                gtk_toggle_button_get_active(
		                             GTK_TOGGLE_BUTTON(advanced_check));

		/* Set goals disabled flag */
		opt.disable_goal = (opt.expanded >= 1) &&
		                gtk_toggle_button_get_active(
		                         GTK_TOGGLE_BUTTON(disable_goal_check));

		/* Set takeover disabled flag */
		opt.disable_takeover = (opt.expanded >= 2) &&
		                gtk_toggle_button_get_active(
		                     GTK_TOGGLE_BUTTON(disable_takeover_check));

		/* Apply options */
		apply_options();

		/* Recreate GUI elements for new number of players */
		modify_gui();

		/* Force game over */
		real_game.game_over = 1;

		/* Start new game */
		restart_loop = RESTART_NEW;

		/* Save preferences */
		save_prefs();

		/* Quit waiting for events */
		gtk_main_quit();
	}

	/* Destroy dialog */
	gtk_widget_destroy(dialog);
}

/*
 * Modify GUI options.
 */
static void gui_options(GtkMenuItem *menu_item, gpointer data)
{
	GtkWidget *dialog;
	GtkWidget *radio = NULL;
	GtkWidget *reduce_box;
	GtkWidget *reduce_frame;
	GtkWidget *shrink_button;
	int i;

	/* Create dialog box */
	dialog = gtk_dialog_new_with_buttons("GUI Options", NULL,
	                                     GTK_DIALOG_MODAL,
	                                     GTK_STOCK_OK,
                                             GTK_RESPONSE_ACCEPT,
	                                     GTK_STOCK_CANCEL,
	                                     GTK_RESPONSE_REJECT, NULL);

	/* Set window title */
	gtk_window_set_title(GTK_WINDOW(dialog),
	                     "Race for the Galaxy " VERSION);

	/* Create vbox to hold full-size image option radio buttons */
	reduce_box = gtk_vbox_new(FALSE, 0);

	/* Loop over reduction levels */
	for (i = 0; reduce_names[i]; i++)
	{
		/* Create radio button */
		radio = gtk_radio_button_new_with_label_from_widget(
		                                        GTK_RADIO_BUTTON(radio),
		                                        reduce_names[i]);

		/* Check for current reduction level */
		if (opt.full_reduced == i)
		{
			/* Set button active */
			gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(radio),
			                             TRUE);

			/* Remember current reduction level */
			next_reduce = i;
		}

		/* Add handler */
		g_signal_connect(G_OBJECT(radio), "toggled",
		                 G_CALLBACK(reduce_toggle), GINT_TO_POINTER(i));

		/* Pack radio button into box */
		gtk_box_pack_start(GTK_BOX(reduce_box), radio, FALSE, TRUE, 0);
	}

	/* Create frame around buttons */
	reduce_frame = gtk_frame_new("Full-size image");

	/* Pack radio button box into frame */
	gtk_container_add(GTK_CONTAINER(reduce_frame), reduce_box);

	/* Add frame to dialog box */
	gtk_container_add(GTK_CONTAINER(GTK_DIALOG(dialog)->vbox),
	                  reduce_frame);

	/* Create toggle button for shrinking opponent areas */
	shrink_button = gtk_check_button_new_with_label("Shrink Opponents");

	/* Set toggled status */
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(shrink_button),
	                             opt.shrink_opponent);

	/* Add button to dialog box */
	gtk_container_add(GTK_CONTAINER(GTK_DIALOG(dialog)->vbox),
	                  shrink_button);

	/* Show all widgets */
	gtk_widget_show_all(dialog);

	/* Run dialog */
	if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT)
	{
		/* Set full-size image option */
		opt.full_reduced = next_reduce;

		/* Set shrink opponents option */
		opt.shrink_opponent =
		 gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(shrink_button));

		/* Handle new options */
		modify_gui();

		/* Save preferences */
		save_prefs();
	}

	/* Destroy dialog */
	gtk_widget_destroy(dialog);
}

/*
 * Convert player numbers to names.
 */
static void render_player(GtkTreeViewColumn *col, GtkCellRenderer *cell,
                          GtkTreeModel *model, GtkTreeIter *iter,
                          gpointer data)
{
	char *name;
	int i;

	/* Get player number from model */
	gtk_tree_model_get(model, iter, 2, &i, -1);

	/* Check for no player */
	if (i < 0)
	{
		/* Set no name */
		name = "None";
	}
	else
	{
		/* Set to player's name */
		name = real_game.p[i].name;
	}

	/* Set "text" property of renderer */
	g_object_set(cell, "text", name, NULL);
}

/*
 * Convert location numbers to names.
 */
static void render_where(GtkTreeViewColumn *col, GtkCellRenderer *cell,
                         GtkTreeModel *model, GtkTreeIter *iter,
                         gpointer data)
{
	char *name;
	int i;

	/* Get location from model */
	gtk_tree_model_get(model, iter, 3, &i, -1);

	/* Set name string */
	switch (i)
	{
		case WHERE_DECK: name = "Deck"; break;
		case WHERE_DISCARD: name = "Discard"; break;
		case WHERE_HAND: name = "Hand"; break;
		case WHERE_ACTIVE: name = "Active"; break;
		case WHERE_GOOD: name = "Good"; break;
		default: name = "Unknown"; break;
	}

	/* Set "text" property of renderer */
	g_object_set(cell, "text", name, NULL);
}

/*
 * Attempt to place a moved card into the proper display table.
 */
static void debug_card_moved(int c, int old_owner, int old_where)
{
	card *c_ptr;
	displayed *i_ptr;
	int i;

	/* Get card pointer */
	c_ptr = &real_game.deck[c];

	/* Check for moving from our hand */
	if (old_owner == player_us && old_where == WHERE_HAND)
	{
		/* Loop over our hand */
		for (i = 0; i < hand_size; i++)
		{
			/* Get displayed card pointer */
			i_ptr = &hand[i];

			/* Check for match */
			if (i_ptr->index == c)
			{
				/* Remove from hand */
				hand[i] = hand[--hand_size];

				/* Done */
				break;
			}
		}
	}

	/* Check for moving from active area */
	if (old_where == WHERE_ACTIVE)
	{
		/* Loop over table area */
		for (i = 0; i < table_size[old_owner]; i++)
		{
			/* Get displayed card pointer */
			i_ptr = &table[old_owner][i];

			/* Check for match */
			if (i_ptr->index == c)
			{
				/* Remove from table */
				table[old_owner][i] =
				      table[old_owner][--table_size[old_owner]];

				/* Done */
				break;
			}
		}
	}

	/* Check for adding card to our hand */
	if (c_ptr->owner == player_us && c_ptr->where == WHERE_HAND)
	{
		/* Add card to hand */
		i_ptr = &hand[hand_size++];

		/* Set design and index */
		i_ptr->d_ptr = c_ptr->d_ptr;
		i_ptr->index = c;

		/* Clear all other fields */
		i_ptr->eligible = i_ptr->gapped = 0;
		i_ptr->selected = i_ptr->color = 0;
		i_ptr->covered = 0;
	}

	/* Check for adding card to active area */
	if (c_ptr->owner != -1 && c_ptr->where == WHERE_ACTIVE)
	{
		/* Add card to hand */
		i_ptr = &table[c_ptr->owner][table_size[c_ptr->owner]++];

		/* Set design and index */
		i_ptr->d_ptr = c_ptr->d_ptr;
		i_ptr->index = c;

		/* Clear all other fields */
		i_ptr->eligible = i_ptr->gapped = 0;
		i_ptr->selected = i_ptr->color = 0;
		i_ptr->covered = 0;
	}

	/* Redraw */
	redraw_everything();
}

/*
 * Called when the player cell of the debug window has been edited.
 */
static void player_edit(GtkCellRendererCombo *cell, char *path_str, char *text,
                        gpointer data)
{
	GtkTreeModel *model = GTK_TREE_MODEL(data);
	GtkTreePath *path;
	GtkTreeIter iter;
	card *c_ptr;
	int c, i, old_owner;

	/* Create path from path string */
	path = gtk_tree_path_new_from_string(path_str);

	/* Get iterator for path */
	gtk_tree_model_get_iter(model, &iter, path);

	/* Get card index for this row */
	gtk_tree_model_get(model, &iter, 0, &c, -1);

	/* Get card pointer */
	c_ptr = &real_game.deck[c];

	/* Remember current owner */
	old_owner = c_ptr->owner;

	/* Check for setting to "None" */
	if (!strcmp(text, "None"))
	{
		/* Clear owner */
		c_ptr->owner = -1;
	}

	/* Loop over players */
	for (i = 0; i < real_game.num_players; i++)
	{
		/* Check for matching name */
		if (!strcmp(text, real_game.p[i].name))
		{
			/* Set owner */
			c_ptr->owner = i;
		}
	}

	/* Store new player number in model */
	gtk_list_store_set(GTK_LIST_STORE(model), &iter, 2, c_ptr->owner, -1);

	/* Notice card movement */
	debug_card_moved(c, old_owner, c_ptr->where);
}

/*
 * Called when the location cell of the debug window has been edited.
 */
static void where_edit(GtkCellRendererCombo *cell, char *path_str, char *text,
                       gpointer data)
{
	GtkTreeModel *model = GTK_TREE_MODEL(data);
	GtkTreePath *path;
	GtkTreeIter iter;
	card *c_ptr;
	int c, old_where;

	/* Create path from path string */
	path = gtk_tree_path_new_from_string(path_str);

	/* Get iterator for path */
	gtk_tree_model_get_iter(model, &iter, path);

	/* Get card index for this row */
	gtk_tree_model_get(model, &iter, 0, &c, -1);

	/* Get card pointer */
	c_ptr = &real_game.deck[c];

	/* Remember current location */
	old_where = c_ptr->where;

	/* Set location based on string */
	if (!strcmp(text, "Deck")) c_ptr->where = WHERE_DECK;
	else if (!strcmp(text, "Discard")) c_ptr->where = WHERE_DISCARD;
	else if (!strcmp(text, "Hand")) c_ptr->where = WHERE_HAND;
	else if (!strcmp(text, "Active")) c_ptr->where = WHERE_ACTIVE;
	else if (!strcmp(text, "Good")) c_ptr->where = WHERE_GOOD;

	/* Store new location in model */
	gtk_list_store_set(GTK_LIST_STORE(model), &iter, 3, c_ptr->where, -1);

	/* Notice card movement */
	debug_card_moved(c, c_ptr->owner, old_where);
}

/*
 * Show a "debug" dialog to give players cards, etc.
 */
static void debug_card_dialog(GtkMenuItem *menu_item, gpointer data)
{
	GtkWidget *dialog;
	GtkWidget *list_view, *list_scroll;
	GtkListStore *card_list, *player_list, *where_list;
	GtkTreeIter list_iter;
	GtkCellRenderer *render;
	card *c_ptr;
	int i;

	/* Check for connected to server */
	if (client_state != CS_DISCONN) return;

	/* Create dialog box */
	dialog = gtk_dialog_new_with_buttons("Debug", NULL, 0,
					     GTK_STOCK_OK,
                                             GTK_RESPONSE_ACCEPT, NULL);

	/* Set window title */
	gtk_window_set_title(GTK_WINDOW(dialog),
	                     "Race for the Galaxy " VERSION);

	/* Set default height */
	gtk_window_set_default_size(GTK_WINDOW(dialog), -1, 600);

	/* Create a card list */
	card_list = gtk_list_store_new(4, G_TYPE_INT, G_TYPE_STRING,
	                                  G_TYPE_INT, G_TYPE_INT);

	/* Loop over cards */
	for (i = 0; i < real_game.deck_size; i++)
	{
		/* Get card pointer */
		c_ptr = &real_game.deck[i];

		/* Add new row to card list */
		gtk_list_store_append(card_list, &list_iter);

		/* Set card information */
		gtk_list_store_set(card_list, &list_iter,
		                   0, i,
		                   1, c_ptr->d_ptr->name,
		                   2, c_ptr->owner,
		                   3, c_ptr->where,
		                   -1);
	}

	/* Create a player list */
	player_list = gtk_list_store_new(2, G_TYPE_INT, G_TYPE_STRING);

	/* Add row for "None" player */
	gtk_list_store_append(player_list, &list_iter);

	/* Set no name and index */
	gtk_list_store_set(player_list, &list_iter, 0, -1, 1, "None", -1);

	/* Loop over players */
	for (i = 0; i < real_game.num_players; i++)
	{
		/* Add new row to player list */
		gtk_list_store_append(player_list, &list_iter);

		/* Set player name and index */
		gtk_list_store_set(player_list, &list_iter,
		                   0, i,
		                   1, real_game.p[i].name,
		                   -1);
	}

	/* Create a list of card locations */
	where_list = gtk_list_store_new(2, G_TYPE_INT, G_TYPE_STRING);

	/* Add row for "Deck" */
	gtk_list_store_append(where_list, &list_iter);
	gtk_list_store_set(where_list, &list_iter,
	                   0, WHERE_DECK, 1, "Deck", -1);

	/* Add row for "Discard" */
	gtk_list_store_append(where_list, &list_iter);
	gtk_list_store_set(where_list, &list_iter,
	                   0, WHERE_DISCARD, 1, "Discard", -1);

	/* Add row for "Hand" */
	gtk_list_store_append(where_list, &list_iter);
	gtk_list_store_set(where_list, &list_iter,
	                   0, WHERE_HAND, 1, "Hand", -1);

	/* Add row for "Active" */
	gtk_list_store_append(where_list, &list_iter);
	gtk_list_store_set(where_list, &list_iter,
	                   0, WHERE_ACTIVE, 1, "Active", -1);

	/* Add row for "Good" */
	gtk_list_store_append(where_list, &list_iter);
	gtk_list_store_set(where_list, &list_iter,
	                   0, WHERE_GOOD, 1, "Good", -1);

	/* Create view of card list */
	list_view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(card_list));

	/*** First column (card name) ***/

	/* Create text renderer */
	render = gtk_cell_renderer_text_new();

	/* Create list view column */
	gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(list_view),
	                                            -1, "Card Name", render,
	                                            "text", 1, NULL);

	/*** Second column (card owner) ***/

	/* Create combo box renderer */
	render = gtk_cell_renderer_combo_new();

	/* Set renderer properties */
	g_object_set(render, "text-column", 1, "model", player_list,
	             "editable", TRUE, "has-entry", FALSE, NULL);

	/* Connect "edited" signal */
	g_signal_connect(render, "edited", G_CALLBACK(player_edit), card_list);

	/* Create list view column */
	gtk_tree_view_insert_column_with_data_func(GTK_TREE_VIEW(list_view),
	                                           -1, "Owner", render,
	                                           render_player, NULL,
	                                           NULL);

	/*** Third column (card location) ***/

	/* Create combo box renderer */
	render = gtk_cell_renderer_combo_new();

	/* Set renderer properties */
	g_object_set(render, "text-column", 1, "model", where_list,
	             "editable", TRUE, "has-entry", FALSE, NULL);

	/* Connect "edited" signal */
	g_signal_connect(render, "edited", G_CALLBACK(where_edit), card_list);

	/* Create list view column */
	gtk_tree_view_insert_column_with_data_func(GTK_TREE_VIEW(list_view),
	                                           -1, "Location", render,
	                                           render_where, NULL,
	                                           NULL);

	/* Create scrolled window for list view */
	list_scroll = gtk_scrolled_window_new(NULL, NULL);

	/* Add list view to scrolled window */
	gtk_container_add(GTK_CONTAINER(list_scroll), list_view);

	/* Set scrolling policy */
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(list_scroll),
	                               GTK_POLICY_NEVER,
	                               GTK_POLICY_ALWAYS);
	
	/* Add scrollable list view to dialog */
	gtk_container_add(GTK_CONTAINER(GTK_DIALOG(dialog)->vbox), list_scroll);

	/* Show everything */
	gtk_widget_show_all(dialog);

	/* Run dialog */
	gtk_dialog_run(GTK_DIALOG(dialog));

	/* Destroy dialog */
	gtk_widget_destroy(dialog);
}

/*
 * Action names/action combination names.
 */
static char *ai_debug_action[2][23] =
{
	{
		"Explore +5",
		"Explore +1,+1",
		"Develop",
		"Settle",
		"Consume-Trade",
		"Consume-x2",
		"Produce"
	},

	{
		"E5/E1",
		"E5/D",
		"E5/S",
		"E5/CT",
		"E5/C2",
		"E5/P",
		"E1/D",
		"E1/S",
		"E1/CT",
		"E1/C2",
		"E1/P",
		"D/D",
		"D/S",
		"D/CT",
		"D/C2",
		"D/P",
		"S/S",
		"S/CT",
		"S/C2",
		"S/P",
		"CT/C2",
		"CT/P",
		"C2/P"
	}
};

/*
 * Show AI debugging information.
 */
static void debug_ai_dialog(GtkMenuItem *menu_item, gpointer data)
{
	GtkWidget *dialog;
	GtkWidget *label, *table;
	double *role[MAX_PLAYER];
	double *action_score[MAX_PLAYER];
	double win_prob[MAX_PLAYER][MAX_PLAYER];
	int num_action;
	char buf[1024];
	int i, j;

	/* Create dialog box */
	dialog = gtk_dialog_new_with_buttons("Debug", NULL, 0,
					     GTK_STOCK_OK,
                                             GTK_RESPONSE_ACCEPT, NULL);

	/* Set window title */
	gtk_window_set_title(GTK_WINDOW(dialog),
	                     "Race for the Galaxy " VERSION);

	/* Get debug information from AI */
	ai_debug(&real_game, win_prob, role, action_score, &num_action);

	/* Create label */
	label = gtk_label_new("Role choice probabilities:");

	/* Pack label */
	gtk_container_add(GTK_CONTAINER(GTK_DIALOG(dialog)->vbox), label);

	/* Create table for role probabilities */
	table = gtk_table_new(real_game.num_players + 1, num_action + 1, FALSE);

	/* Set spacings between columns */
	gtk_table_set_col_spacings(GTK_TABLE(table), 5);

	/* Loop over action names */
	for (i = 0; i < num_action; i++)
	{
		/* Create label */
		label = gtk_label_new(ai_debug_action[real_game.advanced][i]);

		/* Add label to table */
		gtk_table_attach_defaults(GTK_TABLE(table), label,
		                          i + 1, i + 2, 0, 1);
	}

	/* Loop over players */
	for (i = 0; i < real_game.num_players; i++)
	{
		/* Create label with player name */
		label = gtk_label_new(real_game.p[i].name);

		/* Add label to table */
		gtk_table_attach_defaults(GTK_TABLE(table), label,
		                          0, 1, i + 1, i + 2);

		/* Loop over actions */
		for (j = 0; j < num_action; j++)
		{
			/* Create label text */
			sprintf(buf, "%.2f", role[i][j]);

			/* Create label */
			label = gtk_label_new(buf);

			/* Add label to table */
			gtk_table_attach_defaults(GTK_TABLE(table), label,
			                          j + 1, j + 2, i + 1, i + 2);
		}
	}

	/* Add table to dialog */
	gtk_container_add(GTK_CONTAINER(GTK_DIALOG(dialog)->vbox), table);

	/* Create label */
	label = gtk_label_new("Win probabilities:");

	/* Pack label */
	gtk_container_add(GTK_CONTAINER(GTK_DIALOG(dialog)->vbox), label);

	/* Create table for win probabilities */
	table = gtk_table_new(real_game.num_players + 1,
	                      real_game.num_players + 1, FALSE);

	/* Set spacings between columns */
	gtk_table_set_col_spacings(GTK_TABLE(table), 5);

	/* Loop over players */
	for (i = 0; i < real_game.num_players; i++)
	{
		/* Create label */
		label = gtk_label_new(real_game.p[i].name);

		/* Add label to table */
		gtk_table_attach_defaults(GTK_TABLE(table), label,
		                          i + 1, i + 2, 0, 1);
	}

	/* Loop over player's point of views */
	for (i = 0; i < real_game.num_players; i++)
	{
		/* Create label with player name */
		label = gtk_label_new(real_game.p[i].name);

		/* Add label to table */
		gtk_table_attach_defaults(GTK_TABLE(table), label,
		                          0, 1, i + 1, i + 2);

		/* Loop over target players */
		for (j = 0; j < real_game.num_players; j++)
		{
			/* Create label text */
			sprintf(buf, "%.2f", win_prob[i][j]);

			/* Create label */
			label = gtk_label_new(buf);

			/* Add label to table */
			gtk_table_attach_defaults(GTK_TABLE(table), label,
			                          j + 1, j + 2, i + 1, i + 2);
		}
	}

	/* Add table to dialog */
	gtk_container_add(GTK_CONTAINER(GTK_DIALOG(dialog)->vbox), table);

	/* Create label */
	label = gtk_label_new("Action scores:");

	/* Pack label */
	gtk_container_add(GTK_CONTAINER(GTK_DIALOG(dialog)->vbox), label);

	/* Create table for role probabilities */
	table = gtk_table_new(real_game.num_players + 1, num_action + 1, FALSE);

	/* Set spacings between columns */
	gtk_table_set_col_spacings(GTK_TABLE(table), 5);

	/* Loop over action names */
	for (i = 0; i < num_action; i++)
	{
		/* Create label */
		label = gtk_label_new(ai_debug_action[real_game.advanced][i]);

		/* Add label to table */
		gtk_table_attach_defaults(GTK_TABLE(table), label,
		                          i + 1, i + 2, 0, 1);
	}

	/* Loop over players */
	for (i = 0; i < real_game.num_players; i++)
	{
		/* Create label with player name */
		label = gtk_label_new(real_game.p[i].name);

		/* Add label to table */
		gtk_table_attach_defaults(GTK_TABLE(table), label,
		                          0, 1, i + 1, i + 2);

		/* Loop over actions */
		for (j = 0; j < num_action; j++)
		{
			/* Create label text */
			sprintf(buf, "%.2f", action_score[i][j]);

			/* Create label */
			label = gtk_label_new(buf);

			/* Add label to table */
			gtk_table_attach_defaults(GTK_TABLE(table), label,
						  j + 1, j + 2, i + 1, i + 2);
		}
	}

	/* Add table to dialog */
	gtk_container_add(GTK_CONTAINER(GTK_DIALOG(dialog)->vbox), table);

	/* Show everything */
	gtk_widget_show_all(dialog);

	/* Run dialog */
	gtk_dialog_run(GTK_DIALOG(dialog));

	/* Destroy dialog box */
	gtk_widget_destroy(dialog);

	/* Free rows of role probabilities and action scores */
	for (i = 0; i < real_game.num_players; i++)
	{
		/* Free rows */
		free(role[i]);
		free(action_score[i]);
	}
}


/*
 * Quit.
 */
static void gui_quit_game(GtkMenuItem *menu_item, gpointer data)
{
	/* Quit */
	exit(0);
}

/*
 * Show an "about" dialog.
 */
static void about_dialog(GtkMenuItem *menu_item, gpointer data)
{
	GtkWidget *dialog;
	GtkWidget *image;

	/* Create dialog */
	dialog = gtk_message_dialog_new(NULL, GTK_DIALOG_DESTROY_WITH_PARENT,
	                                GTK_MESSAGE_INFO, GTK_BUTTONS_CLOSE,
	                                "Race for the Galaxy " VERSION);

	/* Set window title */
	gtk_window_set_title(GTK_WINDOW(dialog),
	                     "Race for the Galaxy " VERSION);

	/* Set secondary txet */
	gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog),
"This program is written by Keldon Jones, and the source code is licensed \
under the GNU General Public License.\n\n\
Race for the Galaxy was designed by Tom Lehmann and published by Rio Grande \
Games.  All card and other art is copyrighted by Rio Grande Games.\n\n\
Send bug reports to keldon@keldon.net");

	/* Create image from card back */
	image = gtk_image_new_from_pixbuf(card_back);

	/* Show image */
	gtk_widget_show(image);

	/* Set dialog's image */
	gtk_message_dialog_set_image(GTK_MESSAGE_DIALOG(dialog), image);

	/* Run dialog */
	gtk_dialog_run(GTK_DIALOG(dialog));

	/* Destroy dialog */
	gtk_widget_destroy(dialog);
}

/*
 * Quit from innermost invocation of gtk_main().
 *
 * We make extra calls into gtk_main when waiting for a response from the
 * user.  Quitting the innermost will then continue on once when response
 * is ready.
 */
static void action_pressed(GtkButton *button, gpointer data)
{
	/* Move text separator to bottom */
	reset_text_separator();

	/* Disable action button */
	gtk_widget_set_sensitive(action_button, FALSE);

	/* Reset action prompt */
	gtk_label_set_text(GTK_LABEL(action_prompt), "Waiting for opponents");

	/* Quit innermost loop */
	gtk_main_quit();
}

/*
 * Main window was destroyed.
 */
static void destroy(GtkWidget *widget, gpointer data)
{
	/* Quit */
	exit(0);
}

/*
 * Switch main window area widgets.
 *
 * We do this by hiding/show the appropriate top-level container widgets.
 */
void switch_view(int lobby, int chat)
{
	/* Check for showing lobby */
	if (lobby)
	{
		/* Show lobby box */
		gtk_widget_show(lobby_vbox);

		/* Hide main game box */
		gtk_widget_hide(main_hbox);
	}
	else
	{
		/* Hide lobby box */
		gtk_widget_hide(lobby_vbox);

		/* Show main game box */
		gtk_widget_show(main_hbox);
	}

	/* Check whether to hide/show chat entry */
	if (chat)
	{
		/* Show chat entry */
		gtk_widget_show(entry_hbox);
	}
	else
	{
		/* Hide chat entry */
		gtk_widget_hide(entry_hbox);
	}
}

/*
 * Setup windows, callbacks, etc, then let GTK take over.
 */
int main(int argc, char *argv[])
{
	GtkWidget *window;
	GtkWidget *main_vbox;
	GtkWidget *left_vbox, *right_vbox;
	GtkWidget *chat_hbox, *join_hbox;
	GtkWidget *users_view, *users_scroll;
	GtkWidget *chat_scroll;
	GtkWidget *chat_entry;
	GtkWidget *games_scroll;
	GtkWidget *menu_bar, *game_menu, *debug_menu, *help_menu, *network_menu;
	GtkWidget *game_item, *network_item, *debug_item, *help_item;
	GtkWidget *new_item, *load_item, *select_item, *option_item, *quit_item;
	GtkWidget *debug_card_item, *debug_ai_item, *about_item;
	GtkWidget *connect_item, *disconnect_item;
	GtkWidget *h_sep, *v_sep, *event;
	GtkWidget *msg_scroll;
	GtkWidget *table_box, *active_box;
	GtkWidget *top_box, *top_view, *top_scroll, *area;
	GtkWidget *phase_box, *label;
	GtkSizeGroup *top_size_group;
	GtkTextIter end_iter;
	GtkTextBuffer *message_buffer, *chat_buffer;
	GtkCellRenderer *render, *toggle_render;
	GtkTreeViewColumn *desc_column;
	GdkColor color;
	int i;

#ifdef __APPLE__        
	/* Set cwd to OS X .app bundle Resource fork so relative paths work */
	CFBundleRef mainBundle = CFBundleGetMainBundle();
	CFURLRef resourcesURL = CFBundleCopyResourcesDirectoryURL(mainBundle);
	char path[PATH_MAX];
	if (!CFURLGetFileSystemRepresentation(resourcesURL, TRUE, (UInt8 *)path, PATH_MAX))
	{
		// error! Resources (cards and ai nets) will not load.
	}
	CFRelease(resourcesURL);	
	chdir(path);
#endif
	
	/* Set random seed */
	real_game.random_seed = time(NULL);

	/* Prevent locale usage -- use C locale for everything */
	gtk_disable_setlocale();

	/* Parse GTK options */
	gtk_init(&argc, &argv);

#if 0
	/* Change numeric format to widely portable mode */
	setlocale(LC_NUMERIC, "C");

	/* Bind and set text domain */
	bindtextdomain("rftg", LOCALEDIR);
	textdomain("rftg");

	/* Always provide traslated text in UTF-8 format */
	bind_textdomain_codeset("rftg", "UTF-8");
#endif

	/* Load card designs */
	read_cards();

	/* Load card images */
	load_images();

	/* Read preference file */
	read_prefs();

	/* Parse arguments */
	for (i = 1; i < argc; i++)
	{
		/* Check for number of players */
		if (!strcmp(argv[i], "-p"))
		{
			/* Set number of players */
			opt.num_players = atoi(argv[++i]);
		}

		/* Check for expansion level */
		else if (!strcmp(argv[i], "-e"))
		{
			/* Set expansion level */
			opt.expanded = atoi(argv[++i]);
		}

		/* Check for advanced game */
		else if (!strcmp(argv[i], "-a"))
		{
			/* Set advanced */
			opt.advanced = 1;
		}

		/* Check for random seed */
		else if (!strcmp(argv[i], "-r"))
		{
			/* Set random seed */
			real_game.random_seed = atoi(argv[++i]);
		}
	}

	/* Apply options */
	apply_options();

	/* Reset game's player structures */
	reset_gui();

	/* Create choice logs for each player */
	for (i = 0; i < MAX_PLAYER; i++)
	{
		/* Create log */
		real_game.p[i].choice_log = (int *)malloc(sizeof(int) * 4096);
	}

	/* Create toplevel window */
	window = gtk_window_new(GTK_WINDOW_TOPLEVEL);

	/* Window default size */
	gtk_window_set_default_size(GTK_WINDOW(window), 1024, 800);

	/* Set window title */
	gtk_window_set_title(GTK_WINDOW(window),
	                     "Race for the Galaxy " VERSION);

	/* Handle main window destruction */
	g_signal_connect(G_OBJECT(window), "destroy", G_CALLBACK(destroy),
	                 NULL);

	/* Create keyboard accelerator group for main window */
	window_accel = gtk_accel_group_new();

	/* Associate accelerator group with main window */;
	gtk_window_add_accel_group(GTK_WINDOW(window), window_accel);

	/* Create "selected by keypress" signal */
	g_signal_new("key-signal", gtk_event_box_get_type(), G_SIGNAL_ACTION,
                     0, NULL, NULL, g_cclosure_marshal_VOID__VOID, G_TYPE_NONE,
	             0);
	g_signal_new("key-signal", gtk_toggle_button_get_type(),G_SIGNAL_ACTION,
                     0, NULL, NULL, g_cclosure_marshal_VOID__VOID, G_TYPE_NONE,
	             0);

	/* Create main vbox to hold menu bar, then rest of game area */
	main_vbox = gtk_vbox_new(FALSE, 0);

	/* Create menu bar */
	menu_bar = gtk_menu_bar_new();

	/* Create menu item for 'game' menu */
	game_item = gtk_menu_item_new_with_label("Game");

	/* Add game item to menu bar */
	gtk_menu_shell_append(GTK_MENU_SHELL(menu_bar), game_item);

	/* Create menu item for 'network' menu */
	network_item = gtk_menu_item_new_with_label("Network");

	/* Add network item to menu bar */
	gtk_menu_shell_append(GTK_MENU_SHELL(menu_bar), network_item);

	/* Create menu item for 'debug' menu */
	debug_item = gtk_menu_item_new_with_label("Debug");

	/* Add debug item to menu bar */
	gtk_menu_shell_append(GTK_MENU_SHELL(menu_bar), debug_item);

	/* Create menu item for 'help' menu */
	help_item = gtk_menu_item_new_with_label("Help");

	/* Add help item to menu bar */
	gtk_menu_shell_append(GTK_MENU_SHELL(menu_bar), help_item);

	/* Create game menu */
	game_menu = gtk_menu_new();

	/* Create game menu items */
	new_item = gtk_menu_item_new_with_label("New"); 
	load_item = gtk_menu_item_new_with_label("Load Game..."); 
	save_item = gtk_menu_item_new_with_label("Save Game..."); 
	undo_item = gtk_menu_item_new_with_label("Undo Turn");
	select_item = gtk_menu_item_new_with_label("Select Parameters...");
	option_item = gtk_menu_item_new_with_label("GUI Options...");
	quit_item = gtk_menu_item_new_with_label("Quit"); 

	/* Add items to game menu */
	gtk_menu_shell_append(GTK_MENU_SHELL(game_menu), new_item);
	gtk_menu_shell_append(GTK_MENU_SHELL(game_menu), load_item);
	gtk_menu_shell_append(GTK_MENU_SHELL(game_menu), save_item);
	gtk_menu_shell_append(GTK_MENU_SHELL(game_menu), undo_item);
	gtk_menu_shell_append(GTK_MENU_SHELL(game_menu), select_item);
	gtk_menu_shell_append(GTK_MENU_SHELL(game_menu), option_item);
	gtk_menu_shell_append(GTK_MENU_SHELL(game_menu), quit_item);

	/* Create network menu */
	network_menu = gtk_menu_new();

	/* Create network menu items */
	connect_item = gtk_menu_item_new_with_label("Connect to server...");
	disconnect_item = gtk_menu_item_new_with_label("Disconnect");

	/* Add items to network menu */
	gtk_menu_shell_append(GTK_MENU_SHELL(network_menu), connect_item);
	gtk_menu_shell_append(GTK_MENU_SHELL(network_menu), disconnect_item);

	/* Create debug menu */
	debug_menu = gtk_menu_new();

	/* Create debug menu items */
	debug_card_item = gtk_menu_item_new_with_label("Debug cards...");
	debug_ai_item = gtk_menu_item_new_with_label("Debug AI...");

	/* Add items to debug menu */
	gtk_menu_shell_append(GTK_MENU_SHELL(debug_menu), debug_card_item);
	/* gtk_menu_shell_append(GTK_MENU_SHELL(debug_menu), debug_ai_item); */

	/* Create help menu */
	help_menu = gtk_menu_new();

	/* Create about menu item */
	about_item = gtk_menu_item_new_with_label("About...");

	/* Add item to help menu */
	gtk_menu_shell_append(GTK_MENU_SHELL(help_menu), about_item);

	/* Attach events to menu items */
	g_signal_connect(G_OBJECT(new_item), "activate",
	                 G_CALLBACK(gui_new_game), NULL);
	g_signal_connect(G_OBJECT(load_item), "activate",
	                 G_CALLBACK(gui_load_game), NULL);
	g_signal_connect(G_OBJECT(save_item), "activate",
	                 G_CALLBACK(gui_save_game), NULL);
	g_signal_connect(G_OBJECT(undo_item), "activate",
	                 G_CALLBACK(gui_undo_game), NULL);
	g_signal_connect(G_OBJECT(select_item), "activate",
	                 G_CALLBACK(select_parameters), NULL);
	g_signal_connect(G_OBJECT(option_item), "activate",
	                 G_CALLBACK(gui_options), NULL);
	g_signal_connect(G_OBJECT(quit_item), "activate",
	                 G_CALLBACK(gui_quit_game), NULL);
	g_signal_connect(G_OBJECT(connect_item), "activate",
	                 G_CALLBACK(connect_dialog), NULL);
	g_signal_connect(G_OBJECT(disconnect_item), "activate",
	                 G_CALLBACK(disconnect_server), NULL);
	g_signal_connect(G_OBJECT(debug_card_item), "activate",
	                 G_CALLBACK(debug_card_dialog), NULL);
	g_signal_connect(G_OBJECT(debug_ai_item), "activate",
	                 G_CALLBACK(debug_ai_dialog), NULL);
	g_signal_connect(G_OBJECT(about_item), "activate",
	                 G_CALLBACK(about_dialog), NULL);

	/* Set submenus */
	gtk_menu_item_set_submenu(GTK_MENU_ITEM(game_item), game_menu);
	gtk_menu_item_set_submenu(GTK_MENU_ITEM(network_item), network_menu);
	gtk_menu_item_set_submenu(GTK_MENU_ITEM(debug_item), debug_menu);
	gtk_menu_item_set_submenu(GTK_MENU_ITEM(help_item), help_menu);

	/* Create main hbox to contain status box and game area box */
	main_hbox = gtk_hbox_new(FALSE, 0);

	/* Create left vbox for status information */
	left_vbox = gtk_vbox_new(FALSE, 0);

	/* Create "card view" image */
	full_image = gtk_image_new();

	/* Create separator for status info */
	h_sep = gtk_hseparator_new();

	/* Pack image and separator into left vbox */
	gtk_box_pack_start(GTK_BOX(left_vbox), full_image, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(left_vbox), h_sep, FALSE, FALSE, 0);

	/* Create game status label */
	game_status = gtk_label_new("");

	/* Have game status request minimum width */
	gtk_widget_set_size_request(game_status, CARD_WIDTH, -1);

	/* Add status to status vbox */
	gtk_box_pack_start(GTK_BOX(left_vbox), game_status, FALSE, FALSE, 0);

	/* Create text view for message area */
	message_view = gtk_text_view_new();

	/* Set text wrapping mode */
	gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(message_view), GTK_WRAP_WORD);

	/* Make text uneditable */
	gtk_text_view_set_editable(GTK_TEXT_VIEW(message_view), FALSE);

	/* Hide cursor */
	gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(message_view), FALSE);

	/* Get message buffer */
	message_buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(message_view));

	/* Get iterator for end of buffer */
	gtk_text_buffer_get_end_iter(message_buffer, &end_iter);

	/* Get mark at end of buffer */
	message_end = gtk_text_buffer_create_mark(message_buffer, NULL,
	                                          &end_iter, FALSE);

	/* Connect "expose-event" */
	g_signal_connect_after(G_OBJECT(message_view), "expose-event",
	                       G_CALLBACK(message_view_expose), NULL);

	/* Make scrolled window for message buffer */
	msg_scroll = gtk_scrolled_window_new(NULL, NULL);

	/* Add message buffer to scrolled window */
	gtk_container_add(GTK_CONTAINER(msg_scroll), message_view);

	/* Never scroll horizontally; always vertically */
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(msg_scroll),
	                               GTK_POLICY_NEVER,
	                               GTK_POLICY_ALWAYS);

	/* Pack message buffer into status box */
	gtk_box_pack_start(GTK_BOX(left_vbox), msg_scroll, TRUE, TRUE, 0);

	/* Create right vbox for game area */
	right_vbox = gtk_vbox_new(FALSE, 0);

	/* Create table area box */
	table_box = gtk_hbox_new(FALSE, 0);

	/* Create area to display active goals */
	goal_area = gtk_fixed_new();

	/* Give widget its own window */
	gtk_fixed_set_has_window(GTK_FIXED(goal_area), TRUE);

	/* Set goal area minimum width */
	gtk_widget_set_size_request(goal_area, 70, 0);

	/* Create vbox for active card areas */
	active_box = gtk_vbox_new(FALSE, 0);

	/* Create area for opponents */
	top_box = gtk_hbox_new(FALSE, 0);

	/* Create size group for opponent boxes */
	top_size_group = gtk_size_group_new(GTK_SIZE_GROUP_HORIZONTAL);

	/* Loop over players */
	for (i = 0; i < MAX_PLAYER; i++)
	{
		/* Create box to hold status and table area for this player */
		player_box[i] = gtk_vbox_new(FALSE, 0);

		/* Create box to hold player status information */
		player_status[i] = gtk_hbox_new(FALSE, 0);

		/* Create event box to put status in window */
		event = gtk_event_box_new();

		/* Add player status box to event box */
		gtk_container_add(GTK_CONTAINER(event), player_status[i]);

		/* Save status area pointer */
		orig_status[i] = player_status[i];

		/* Create area for active cards */
		area = gtk_fixed_new();

		/* Save area pointer */
		player_area[i] = area;
		orig_area[i] = area;

		/* Give widget its own window */
		gtk_fixed_set_has_window(GTK_FIXED(area), TRUE);

		/* Lookup player's color */
		gdk_color_parse(player_colors[i], &color);

		/* Set area's background color */
		gtk_widget_modify_bg(area, GTK_STATE_NORMAL, &color);

		/* Have area negotiate new size when needed */
		g_signal_connect(G_OBJECT(area), "size-request",
				 G_CALLBACK(table_request), NULL);

		/* Pack status box and table area into hbox */
		gtk_box_pack_start(GTK_BOX(player_box[i]), area, TRUE, TRUE, 0);
		gtk_box_pack_start(GTK_BOX(player_box[i]), event,
		                   FALSE, FALSE, 0);

		/* Check for opponent */
		if (i != player_us)
		{
			/* Create separator */
			player_sep[i] = gtk_vseparator_new();

			/* Pack separator between opponent boxes */
			gtk_box_pack_start(GTK_BOX(top_box), player_sep[i],
			                   FALSE, FALSE, 0);

			/* Pack player's box into top (opponent) box */
			gtk_box_pack_start(GTK_BOX(top_box), player_box[i],
			                   TRUE, TRUE, 0);

			/* Add opponent box to size group */
			gtk_size_group_add_widget(top_size_group,player_box[i]);
		}
	}

	/* Request sizes for status areas */
	status_resize();
	
	/* Create viewport for opponent boxes */
	top_view = gtk_viewport_new(NULL, NULL);

	/* Add opponent boxes to viewport */
	gtk_container_add(GTK_CONTAINER(top_view), top_box);

	/* Do not draw shadow around boxes */
	gtk_viewport_set_shadow_type(GTK_VIEWPORT(top_view), GTK_SHADOW_NONE);

	/* Create scrollable area for opponent boxes */
	top_scroll = gtk_scrolled_window_new(NULL, NULL);

	/* Add opponent box viewport to scrolled window */
	gtk_container_add(GTK_CONTAINER(top_scroll), top_view);

	/* Never show vertical scroll, sometimes show horizontal */
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(top_scroll),
	                               GTK_POLICY_AUTOMATIC, GTK_POLICY_NEVER);

	/* Pack active card vbox */
	gtk_box_pack_start(GTK_BOX(active_box), top_scroll, TRUE, TRUE, 0);

	/* Create separator between opponent and our area */
	h_sep = gtk_hseparator_new();

	/* Pack separator and our table area */
	gtk_box_pack_start(GTK_BOX(active_box), h_sep, FALSE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(active_box), player_box[player_us], TRUE,
	                   TRUE, 0);

	/* Create separator between goal area and active area */
	v_sep = gtk_vseparator_new();

	/* Pack geal and active areas into table box */
	gtk_box_pack_start(GTK_BOX(table_box), active_box, TRUE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(table_box), v_sep, FALSE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(table_box), goal_area, FALSE, TRUE, 0);

	/* Create area for our hand of cards */
	hand_area = gtk_fixed_new();

	/* Have hand area negotiate new size when needed */
	g_signal_connect(G_OBJECT(hand_area), "size-request",
	                 G_CALLBACK(hand_request), NULL);

	/* Redraw card area when resized */
	g_signal_connect(G_OBJECT(hand_area), "size-allocate",
	                 G_CALLBACK(hand_allocated), NULL);

	/* Redraw goal area when resized */
	g_signal_connect(G_OBJECT(goal_area), "size-allocate",
	                 G_CALLBACK(goal_allocated), NULL);

	/* Create hbox for phase buttons/indicators */
	phase_box = gtk_hbox_new(TRUE, 0);

	/* Create labels for phase indicators */
	for (i = ACT_EXPLORE_5_0; i < MAX_ACTION; i++)
	{
		/* Skip some actions */
		if (i == ACT_EXPLORE_1_1 || i == ACT_CONSUME_X2) continue;

		/* Create label */
		label = gtk_label_new("");

		/* Pack label into phase box */
		gtk_box_pack_start(GTK_BOX(phase_box), label, TRUE, TRUE, 0);

		/* Remember label widget */
		phase_labels[i] = label;
	}

	/* Create box for action area */
	action_box = gtk_hbox_new(FALSE, 0);

	/* Set minimum height for action box */
	gtk_widget_set_size_request(action_box, -1, 35);

	/* Create action prompt */
	action_prompt = gtk_label_new("Action");

	/* Create action button */
	action_button = gtk_button_new_with_label("Done");

	/* Attach event */
	g_signal_connect(G_OBJECT(action_button), "clicked",
	                 G_CALLBACK(action_pressed), NULL);

	/* Set CAN_DEFAULT flag on action button */
	GTK_WIDGET_SET_FLAGS(action_button, GTK_CAN_DEFAULT);

	/* Set action button as default widget */
	gtk_window_set_default(GTK_WINDOW(window), action_button);

	/* Pack laben and button into action box */
	gtk_box_pack_start(GTK_BOX(action_box), action_prompt, TRUE, TRUE, 0);
	gtk_box_pack_end(GTK_BOX(action_box), action_button, FALSE, TRUE, 0);

	/* Pack table area into right vbox */
	gtk_box_pack_start(GTK_BOX(right_vbox), table_box, TRUE, TRUE, 0);

	/* Create separator between our area and phase indicator */
	h_sep = gtk_hseparator_new();

	/* Pack separator and status area */
	gtk_box_pack_start(GTK_BOX(right_vbox), h_sep, FALSE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(right_vbox), phase_box, FALSE, TRUE, 0);

	/* Create separator between phase indicator and action area */
	h_sep = gtk_hseparator_new();

	/* Pack separator and action area */
	gtk_box_pack_start(GTK_BOX(right_vbox), h_sep, FALSE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(right_vbox), action_box, FALSE, TRUE, 0);

	/* Create separator between action area and hand area */
	h_sep = gtk_hseparator_new();

	/* Pack separator and hand area */
	gtk_box_pack_start(GTK_BOX(right_vbox), h_sep, FALSE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(right_vbox), hand_area, FALSE, TRUE, 0);

	/* Create vertical separators between areas */
	v_sep = gtk_vseparator_new();

	/* Pack vbox's into main hbox */
	gtk_box_pack_start(GTK_BOX(main_hbox), left_vbox, FALSE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(main_hbox), v_sep, FALSE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(main_hbox), right_vbox, TRUE, TRUE, 0);

	/* Create vbox for lobby window elements */
	lobby_vbox = gtk_vbox_new(FALSE, 5);

	/* Create list of open games */
	game_list = gtk_tree_store_new(13, G_TYPE_INT, G_TYPE_STRING,
	                                   G_TYPE_STRING, G_TYPE_INT,
	                                   G_TYPE_STRING, G_TYPE_STRING,
	                                   G_TYPE_INT, G_TYPE_INT,
	                                   G_TYPE_INT, G_TYPE_INT,
	                                   G_TYPE_INT, G_TYPE_INT,
	                                   G_TYPE_INT);

	/* Create view for chat users */
	games_view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(game_list));

	/* Create text renderer */
	render = gtk_cell_renderer_text_new();

	/* Create toggle button renderer */
	toggle_render = gtk_cell_renderer_toggle_new();

	/* Create columns for game list */
	gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(games_view),
	                                            -1, "Game Description",
	                                            render, "text", 1, NULL);
	gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(games_view),
	                                            -1, "Created By",
	                                            render, "text", 2, NULL);
	gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(games_view),
	                                            -1, "Password Needed",
	                                            toggle_render, "active",
	                                            3, "visible", 11, NULL);
	gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(games_view),
	                                            -1, "# Players", render,
	                                            "text", 4, NULL);
	gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(games_view),
	                                            -1, "Exp", render,
	                                            "text", 5, NULL);
	gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(games_view),
	                                            -1, "2P Advanced",
	                                            toggle_render, "active",
	                                            6, "visible", 11, NULL);
	gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(games_view),
	                                            -1, "Disable Goals",
	                                            toggle_render, "active",
	                                            7, "visible", 11, NULL);
	gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(games_view),
	                                            -1, "Disable Takeovers",
	                                            toggle_render, "active",
	                                            8, "visible", 11, NULL);

	/* Get first column of game view */
	desc_column = gtk_tree_view_get_column(GTK_TREE_VIEW(games_view), 0);

	/* Set expand property of first column */
	gtk_tree_view_column_set_expand(desc_column, TRUE);

	/* Connect "cursor-changed" property of game view */
	g_signal_connect(G_OBJECT(games_view), "cursor-changed",
	                 G_CALLBACK(game_view_changed), NULL);

	/* Create scrolled window for chat users */
	games_scroll = gtk_scrolled_window_new(NULL, NULL);

	/* Add users view to scrolled window */
	gtk_container_add(GTK_CONTAINER(games_scroll), games_view);

	/* Set scrolling policy */
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(games_scroll),
	                               GTK_POLICY_NEVER,
	                               GTK_POLICY_AUTOMATIC);

	/* Create hbox for game join/create buttons */
	join_hbox = gtk_hbox_new(FALSE, 5);

	/* Create button for creating new game */
	create_button = gtk_button_new_with_label("Create Game");

	/* Connect "clicked" signal of create game button */
	g_signal_connect(G_OBJECT(create_button), "clicked",
	                 G_CALLBACK(create_dialog), NULL);

	/* Create join button */
	join_button = gtk_button_new_with_label("Join Game");

	/* Connect "clicked" signal of join game button */
	g_signal_connect(G_OBJECT(join_button), "clicked",
	                 G_CALLBACK(join_game), NULL);

	/* Create leave button */
	leave_button = gtk_button_new_with_label("Leave Game");

	/* Connect "clicked" signal of leave game button */
	g_signal_connect(G_OBJECT(leave_button), "clicked",
	                 G_CALLBACK(leave_game), NULL);

	/* Create kick player button */
	kick_button = gtk_button_new_with_label("Kick Player");

	/* Connect "clicked" signal of kick player button */
	g_signal_connect(G_OBJECT(kick_button), "clicked",
	                 G_CALLBACK(kick_player), NULL);

	/* Create start button */
	start_button = gtk_button_new_with_label("Start Game");

	/* Connect "clicked" signal of start game button */
	g_signal_connect(G_OBJECT(start_button), "clicked",
	                 G_CALLBACK(start_game), NULL);

	/* Create blank filler label */
	label = gtk_label_new("");

	/* Add buttons to join hbox */
	gtk_box_pack_start(GTK_BOX(join_hbox), label, TRUE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(join_hbox), create_button, FALSE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(join_hbox), join_button, FALSE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(join_hbox), leave_button, FALSE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(join_hbox), kick_button, FALSE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(join_hbox), start_button, FALSE, TRUE, 0);

	/* Create blank filler label */
	label = gtk_label_new("");

	/* Add filler label to end of box */
	gtk_box_pack_start(GTK_BOX(join_hbox), label, TRUE, TRUE, 0);

	/* Create separator between games and buttons */
	h_sep = gtk_hseparator_new();

	/* Add open game area to lobby */
	gtk_box_pack_start(GTK_BOX(lobby_vbox), games_scroll, TRUE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(lobby_vbox), h_sep, FALSE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(lobby_vbox), join_hbox, FALSE, TRUE, 0);

	/* Create hbox for lobby users and chat area */
	chat_hbox = gtk_hbox_new(FALSE, 5);

	/* Create list of online users */
	user_list = gtk_list_store_new(1, G_TYPE_STRING);

	/* Create view for chat users */
	users_view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(user_list));

	/* Create text renderer */
	render = gtk_cell_renderer_text_new();

	/* Create column for user list */
	gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(users_view),
	                                            -1, "Users online", render,
	                                            "text", 0, NULL);

	/* Create scrolled window for chat users */
	users_scroll = gtk_scrolled_window_new(NULL, NULL);

	/* Add users view to scrolled window */
	gtk_container_add(GTK_CONTAINER(users_scroll), users_view);

	/* Set scrolling policy */
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(users_scroll),
	                               GTK_POLICY_NEVER,
	                               GTK_POLICY_AUTOMATIC);

	/* Create text buffer for chat */
	chat_buffer = gtk_text_buffer_new(NULL);

	/* Create "bold" tag for usernames */
	gtk_text_buffer_create_tag(chat_buffer, "bold", "weight", "bold", NULL);

	/* Get end of buffer */
	gtk_text_buffer_get_end_iter(chat_buffer, &end_iter);

	/* Create mark at end of buffer */
	gtk_text_buffer_create_mark(chat_buffer, "end", &end_iter, FALSE);

	/* Create text view for chat area */
	chat_view = gtk_text_view_new_with_buffer(chat_buffer);

	/* Set text wrapping mode */
	gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(chat_view), GTK_WRAP_WORD);

	/* Make text uneditable */
	gtk_text_view_set_editable(GTK_TEXT_VIEW(chat_view), FALSE);

	/* Hide cursor */
	gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(chat_view), FALSE);

	/* Make scrolled window for message buffer */
	chat_scroll = gtk_scrolled_window_new(NULL, NULL);

	/* Add message buffer to scrolled window */
	gtk_container_add(GTK_CONTAINER(chat_scroll), chat_view);

	/* Never scroll horizontally; always vertically */
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(chat_scroll),
	                               GTK_POLICY_NEVER,
	                               GTK_POLICY_ALWAYS);

	/* Create separator between user list and chat view */
	v_sep = gtk_vseparator_new();

	/* Add user view and chat view to chat hbox */
	gtk_box_pack_start(GTK_BOX(chat_hbox), users_scroll, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(chat_hbox), v_sep, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(chat_hbox), chat_scroll, TRUE, TRUE, 0);

	/* Create separator between buttons and chat */
	h_sep = gtk_hseparator_new();

	/* Add chat box to lobby vbox */
	gtk_box_pack_start(GTK_BOX(lobby_vbox), h_sep, FALSE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(lobby_vbox), chat_hbox, TRUE, TRUE, 0);

	/* Create hbox for chat entry area */
	entry_hbox = gtk_hbox_new(FALSE, 5);

	/* Create label for our username */
	entry_label = gtk_label_new("");

	/* Create text entry for chat */
	chat_entry = gtk_entry_new();

	/* Set maximum length of text */
	gtk_entry_set_max_length(GTK_ENTRY(chat_entry), 800);

	/* Connect "activate" signal of chat entry */
	g_signal_connect(G_OBJECT(chat_entry), "activate",
	                 G_CALLBACK(send_chat), NULL);

	/* Add label and entry to hbox */
	gtk_box_pack_start(GTK_BOX(entry_hbox), entry_label, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(entry_hbox), chat_entry, TRUE, TRUE, 0);

	/* Pack menu and main areas into main vbox */
	gtk_box_pack_start(GTK_BOX(main_vbox), menu_bar, FALSE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(main_vbox), main_hbox, TRUE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(main_vbox), lobby_vbox, TRUE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(main_vbox), entry_hbox, FALSE, FALSE, 0);

	/* Add main hbox to main window */
	gtk_container_add(GTK_CONTAINER(window), main_vbox);

	/* Show all widgets */
	gtk_widget_show_all(window);

	/* Switch to main game view */
	switch_view(0, 0);

#ifdef __APPLE__
	/* Setup OS X style menus */	
	IgeMacMenuGroup *group = ige_mac_menu_add_app_menu_group();
	ige_mac_menu_add_app_menu_item(group,
	                               GTK_MENU_ITEM(about_item), NULL);
	
	group = ige_mac_menu_add_app_menu_group();
	ige_mac_menu_add_app_menu_item(group,
	                               GTK_MENU_ITEM(option_item),
	                               "Preferences...");
	
	ige_mac_menu_set_quit_menu_item(GTK_MENU_ITEM(quit_item));
	
	gtk_widget_hide(menu_bar);
	ige_mac_menu_set_menu_bar(GTK_MENU_SHELL(menu_bar));	
#endif
	
	/* Modify GUI for current setup */
	modify_gui();

	/* Start new game */
	restart_loop = RESTART_NEW;

	/* Run games */
	run_game();

	/* Exit */
	return 0;
}
