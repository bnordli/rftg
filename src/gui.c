/*
 * Race for the Galaxy AI
 *
 * Copyright (C) 2009-2011 Keldon Jones
 *
 * Source file modified by B. Nordli, August 2014.
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
#include "gtk-mac-menu.h"
#endif

#define TITLE "Race for the Galaxy " RELEASE

/*
 * Our default options.
 */
options opt =
{
	3, // num_players
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
 * Flags for game tampered state.
 */
#define TAMPERED_SAVE 0x1
#define TAMPERED_LOAD 0x2
#define TAMPERED_SEED 0x4
#define TAMPERED_UNDO 0x8
#define TAMPERED_LOOK 0x10
#define TAMPERED_MOVE 0x20

/*
 * Whether the current game has been "tampered" with in some way.
 */
static int game_tampered;

/*
 * Current undo position.
 */
static int num_undo;

/*
 * Total number of undo positions saved.
 */
static int max_undo;

/*
 * Whether the game is replaying or not.
 */
static int game_replaying;

/*
 * Choice logs for each player.
 */
static int *orig_log[MAX_PLAYER];

/*
 * Original log sizes for each player.
 */
static int orig_log_size[MAX_PLAYER];

/*
 * Games started (used for random sampling)
 */
static int games_started;

/*
 * Player we're playing as.
 */
int player_us;

/*
 * We have restarted the main game loop.
 */
int restart_loop;

static char *goal_description[MAX_GOAL] =
{
	"First to have five VP chips",
	"First to have worlds of all four kinds",
	"First to have three Alien cards",
	"First to discard at end of round",
	"First to have powers in all phases, plus Trade",
	"First to place a 6-cost development giving ? VPs",
	"First to have three Uplift cards",
	"First to have four goods",
	"First to have eight cards",
	"First to have negative Military and two worlds\n"
	  " or a takeover attack power and two Military worlds",
	"First to have two prestige chips and three VP chips",
	"First to have three Imperium cards\n"
	  " or four Military worlds",

	"Most total military",
	"Most Novelty and/or Rare worlds",
	"Most developments",
	"Most production worlds",
	"Most cards with Explore powers",
	"Most Rebel Military worlds",
	"Most prestige chips",
	"Most cards with Consume powers",
};

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
 * Colors to highlight with.
 */
#define HIGH_NONE   0
#define HIGH_YELLOW 1
#define HIGH_RED    2

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

	/* Card should deselect all others when selected */
	int greedy;

	/* Card should be highlighted in this color when selected */
	int highlight;

	/* Card should be highlighted in this color when not selected */
	int highlight_else;

	/* Card should be "pushed up" when selected */
	int push;

	/* Card is not eligible for selection, but should be colored anyway */
	int color;

	/* Card is covered by goods */
	int num_goods;

	/* Order card was played in (if on table) */
	int order;

	/* Tooltip to display (if any) */
	char *tooltip;

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

typedef struct discounts
{
	/* The base discount */
	int base;

	/* The current temporary discount */
	int bonus;

	/* Additional specific discount */
	int specific[6];

	/* May discard to place at zero count */
	int zero;

	/* Additional discount when paying for military */
	int pay_discount;

	/* May pay for military with 0 discount (Rebel Cantina) */
	int non_alien_mil_0;

	/* May pay for military with 1 discount (Contact Specialist) */
	int non_alien_mil_1;

	/* May pay for rebel worlds with 2 discount (Rebel Alliance) */
	int rebel_mil_2;

	/* May pay for chromosome worlds (Ravaged Uplift World) */
	int chromo_mil;

	/* May pay for alien worlds (Alien Research Team) */
	int alien_mil;

	/* May discard to conquer with 0 discount (Imperium Invasion Fleet) */
	int conquer_settle_0;

	/* May discard to conquer with 2 discount (Imperium Cloaking Tech) */
	int conquer_settle_2;

	/* Any value is set */
	int has_data;

} discounts;

typedef struct mil_strength
{
	/* Base military */
	int base;

	/* Current temporary military */
	int bonus;

	/* Maximum additional temporary military */
	int max_bonus;

	/* Additional military against rebel worlds */
	int rebel;

	/* Additional specific military */
	int specific[6];

	/* Additional extra defense during takeovers */
	int defense;

	/* Additional military when using attack imperium TO power */
	int attack_imperium;

	/* Name of attack imperium TO power */
	char imp_card[64];

	/* Imperium world played */
	int imperium;

	/* Rebel military world played */
	int military_rebel;

	/* Any value is set */
	int has_data;

} mil_strength;

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

	/* Settle discount */
	discounts discount;

	/* Military strength */
	mil_strength military;

	/* Text of VP tooltip */
	char vp_tip[1024];

	/* Text of discount tooltip */
	char discount_tip[1024];

	/* Text of military strength tooltip */
	char military_tip[1024];

	/* Text of prestige tooltip */
	char prestige_tip[1024];

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
 * Extra text and font string to be drawn on an image.
 */
struct extra_info
{
	char text[1024];
	char *fontstr;
	int border;
	int top_left;
};

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
static int action_payment_bonus;
static int action_cidx, action_oidx;

/*
 * Number of icon images.
 */
#define MAX_ICON 25

/*
 * Special icon numbers.
 */
#define ICON_NO_ACT     10
#define ICON_HANDSIZE   11
#define ICON_VP         12
#define ICON_MILITARY   13
#define ICON_PRESTIGE   14
#define ICON_WAITING    15
#define ICON_READY      16
#define ICON_OPTION     17
#define ICON_DISCARD    18
#define ICON_VP_EMPTY   19
#define ICON_DISCOUNT   20
#define ICON_DRAW       21
#define ICON_DRAW_EMPTY 22
#define ICON_EXPLORE    23
#define ICON_CONSUME    24

/*
 * Number of action card images.
 */
#define MAX_ACT_CARD   11


/*
 * Card images.
 */
static GdkPixbuf *image_cache[AVAILABLE_DESIGN];

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
static GdkPixbuf *action_cache[MAX_ACT_CARD];

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
static GtkWidget *phase_box, *action_box;
static GtkWidget *new_item, *new_parameters_item;
static GtkWidget *load_item, *replay_item, *save_item, *export_item;
static GtkWidget *undo_item, *undo_round_item, *undo_game_item;
static GtkWidget *redo_item, *redo_round_item, *redo_game_item;
static GtkWidget *option_item, *advanced_item, *quit_item;
static GtkWidget *debug_card_item, *debug_ai_item, *about_item;
static GtkWidget *connect_item, *disconnect_item, *resign_item;
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
GtkWidget *addai_button, *start_button;
GtkWidget *action_prompt, *action_button;

/*
 * Keyboard accelerator group for main window.
 */
static GtkAccelGroup *window_accel;

#define MAX_ACCEL 18

/*
 * List of accelerator keys.
 */
static unsigned int accel_keys[MAX_ACCEL];

/*
 * List of accelerator modifiers.
 */
static GdkModifierType accel_mods[MAX_ACCEL];

/*
 * Map from actions to accelerators.
 */
static unsigned int act_to_accel[] = {5, 0, 9, 1, 10, 2, 11, 3, 12, 4};

/*
 * Whether user has used accel keys during this choice.
 */
static int accel_used;

/*
 * Number of candidates to keep (during search), or save.
 * TODO: Make a WHERE_REVEALED location.
 */
static int num_special_cards;

/*
 * List of special gui cards.
 */
static card *special_cards[20];

/*
 * Text buffer for message area.
 */
GtkWidget *message_view;

/*
 * Mark at end of message area text buffer.
 */
GtkTextMark *message_end;

/*
 * y-coordinate of line of "last seen" text in buffer.
 */
static int message_last_y;

/*
 * Check whether a log position marks a round boundary.
 */
int is_round_boundary(int advanced, int *p)
{
	/* Only start and action choices are boundary */
	if (*p != CHOICE_START && *p != CHOICE_ACTION) return FALSE;

	/* Second choice of Psi-Crystal is not a boundary */
	/* XXX This only works in newer save games */
	if (advanced && *(p + 1) == 2) return FALSE;

	/* Everything else is */
	return TRUE;
}

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
 * Add formatted text to the message buffer.
 */
void message_add_formatted(game *g, char *msg, char *tag)
{
	GtkTextIter end_iter;
	GtkTextBuffer *message_buffer;

	/* Check for empty tag */
	if (!strlen(tag))
	{
		/* Add unformatted message */
		message_add(g, msg);
		return;
	}

	/* Do not log verbose message while verbosity is disabled */
	if (!strcmp(tag, FORMAT_VERBOSE) && !opt.verbose_log) return;

	/* Do not log draw messages while draw log is disabled */
	if (!strcmp(tag, FORMAT_DRAW) && !opt.draw_log) return;

	/* Do not log discard messages while discard log is disabled */
	if (!strcmp(tag, FORMAT_DISCARD) && !opt.discard_log) return;

	/* Check for emphasized message formatting */
	if (strcmp(tag, FORMAT_EM) && !opt.colored_log)
	{
		/* Skip coloring when colored log is off */
		message_add(g, msg);
		return;
	}

	/* Get message buffer */
	message_buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(message_view));

	/* Get end mark */
	gtk_text_buffer_get_iter_at_mark(message_buffer, &end_iter,
	                                 message_end);

	/* Add formatted message */
	gtk_text_buffer_insert_with_tags_by_name(message_buffer,
	                                         &end_iter,
	                                         msg, -1, tag,
	                                         NULL);

	/* Scroll to end mark */
	gtk_text_view_scroll_mark_onscreen(GTK_TEXT_VIEW(message_view),
	                                   message_end);
}

/*
 * Add a private message to the message buffer.
 */
void message_add_private(game *g, int who, char *msg, char *tag)
{
	/* Verify we are the correct player */
	if (who == player_us)
	{
		/* Add message */
		message_add_formatted(g, msg, tag);
	}
}

/*
 * Handle an error dialog with a message.
 */
void display_error(char *msg)
{
	GtkWidget *alert;

	/* Create error dialog */
	alert = gtk_message_dialog_new(NULL,
	                               GTK_DIALOG_DESTROY_WITH_PARENT,
	                               GTK_MESSAGE_ERROR,
	                               GTK_BUTTONS_CLOSE,
	                               "%s", msg);

	/* Set title */
	gtk_window_set_title(GTK_WINDOW(alert), TITLE);

	/* Run dialog */
	gtk_dialog_run(GTK_DIALOG(alert));

	/* Destroy alert dialog */
	gtk_widget_destroy(alert);
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
 * Update the card image with the given image.
 */
void update_card(GdkPixbuf *newbuf)
{
	static GdkPixbuf *buf;
	GdkPixbuf *scaled_buf;
	double card_width, card_height;

	/* Set image to card back on startup */
	if (!buf) buf = card_back;

	/* Check if image is updated */
	if (newbuf)
	{
		/* Remember the new image */
		buf = newbuf;
	}

	/* Don't do anything if image hidden */
	if (opt.hide_card == 2) return;

	/* Compute card width */
	card_width = opt.card_size ? opt.card_size : CARD_WIDTH;

	/* Compute card height */
	card_height = CARD_HEIGHT * (card_width / CARD_WIDTH);

	/* Scale image */
	scaled_buf = gdk_pixbuf_scale_simple(buf,
	                                     (int) card_width, (int) card_height,
	                                     GDK_INTERP_BILINEAR);

	/* Set image */
	gtk_image_set_from_pixbuf(GTK_IMAGE(full_image), scaled_buf);

	/* Remove our scaled buffer */
	g_object_unref(G_OBJECT(scaled_buf));
}

/*
 * Called when mouse moves over the log window.
 */
static gboolean message_motion(GtkWidget *text_view, GdkEventMotion *event,
                               gpointer data)
{
	int window_x, window_y, buffer_x, buffer_y, i;
	GtkTextIter iter_start, iter_end;
	char *line;

	/* Check for hint event */
	if (event->is_hint)
	{
		/* Extract coordinates */
		gdk_window_get_pointer(event->window, &window_x, &window_y, NULL);
	}
	else
	{
		/* Take coordinates directly */
		window_x = (int) event->x;
		window_y = (int) event->y;
	}

	/* Convert window coordinates to buffer coordinates */
	gtk_text_view_window_to_buffer_coords(GTK_TEXT_VIEW(message_view),
	                                      GTK_TEXT_WINDOW_WIDGET,
	                                      window_x, window_y,
	                                      &buffer_x, &buffer_y);

	/* Get start of line */
	gtk_text_view_get_line_at_y(GTK_TEXT_VIEW(message_view),
	                            &iter_start, buffer_y, NULL);

	/* Get end of line */
	iter_end = iter_start;
	gtk_text_iter_forward_line(&iter_end);

	/* Get line contents */
	line = gtk_text_iter_get_text(&iter_start, &iter_end);

	/* Loop over cards in game */
	for (i = 0; i < real_game.deck_size; i++)
	{
		/* Check if card name is found */
		if (strstr(line, real_game.deck[i].d_ptr->name))
		{
			/* Update image */
			update_card(image_cache[real_game.deck[i].d_ptr->index]);

			/* Card is found */
			break;
		}
	}

	/* Destroy line */
	g_free(line);

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
	GdkPixbuf *tmp_pixbuf;
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
		/* File not found */
		return;
	}

	/* Read header */
	count = g_input_stream_read(fs, buf, 4, NULL, NULL);

	/* Check header */
	if (strncmp(buf, "RFTG", 4))
	{
		/* Error */
		display_error("Error: Image bundle missing header!\n");
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
			count = g_input_stream_read(fs, buf, 3, NULL, NULL);

			/* Convert to integer */
			x = strtol(buf, NULL, 10);

			/* Get pointer to pixbuf holder */
			pix_ptr = &action_cache[x];
		}

		/* Check for something else */
		else
		{
			/* Error */
			display_error("Error: Bad image type!\n");
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
			display_error("Error: Did not read enough image data!\n");
			break;
		}

		/* Create memory stream from image data */
		ms = g_memory_input_stream_new_from_data(data_buf, x, NULL);

		/* Read image from file stream */
		tmp_pixbuf = gdk_pixbuf_new_from_stream(ms, NULL, NULL);

		/* Check for image not already loaded */
		if (!(*pix_ptr))
		{
			/* Use this image */
			*pix_ptr = tmp_pixbuf;
		}
		else
		{
			/* Destroy the unneeded pixbuf */
			g_object_unref(G_OBJECT(tmp_pixbuf));
		}

		/* Close memory stream */
		g_input_stream_close(ms, NULL, NULL);

		/* Free memory */
		free(data_buf);

		/* Check for error */
		if (!(*pix_ptr))
		{
			/* Print error */
			display_error("Error: Could not read image from bundle!\n");
			break;
		}
	}

	/* Close stream */
	g_input_stream_close(fs, NULL, NULL);
}

/*
 * Load one image.
 */
static void load_one_image(char *base_fn, GdkPixbuf **pixbuf)
{
	char *dirs[] = { RFTGDIR "/image/", "image/", "", NULL };
	char fn[1024];
	int i;

	/* Loop over directories */
	for (i = 0; dirs[i]; i++)
	{
		/* Construct filename */
		sprintf(fn, "%s%s", dirs[i], base_fn);

		/* Attempt to load image */
		*pixbuf = gdk_pixbuf_new_from_file(fn, NULL);

		/* Check for success */
		if (*pixbuf) return;
	}
}

/*
 * Load pixbufs with card images.
 */
static int load_images(void)
{
	int i;
	char fn[1024], msg[1024];

	/* Load card back image */
	load_one_image("cardback.jpg", &card_back);

	/* Loop over designs */
	for (i = 0; i < num_design; i++)
	{
		/* Construct image filename */
		sprintf(fn, "card%03d.jpg", i);

		/* Load image */
		load_one_image(fn, &image_cache[i]);
	}

	/* Loop over goals */
	for (i = 0; i < MAX_GOAL; i++)
	{
		/* Construct image filename */
		sprintf(fn, "goal%02d.jpg", i);

		/* Load image */
		load_one_image(fn, &goal_cache[i]);
	}

	/* Loop over icons */
	for (i = 0; i < MAX_ICON; i++)
	{
		/* Skip second develop/settle action */
		if (i == ACT_DEVELOP2 || i == ACT_SETTLE2) continue;

		/* Construct image filename */
		sprintf(fn, "icon%02d.png", i);

		/* Load image */
		load_one_image(fn, &icon_cache[i]);
	}

	/* Loop over actions */
	for (i = 0; i < MAX_ACT_CARD; i++)
	{
		/* Skip second develop/settle action */
		if (i == ACT_DEVELOP2 || i == ACT_SETTLE2) continue;

		/* Construct image filename */
		sprintf(fn, "action%02d.jpg", i);

		/* Load image */
		load_one_image(fn, &action_cache[i]);
	}

	/* Try to load rest of image data from bundle */
	load_image_bundle();

	/* Check for card back image */
	if (!card_back)
	{
		/* Error */
		display_error("Error: Could not load card back image!\n");
		return -1;
	}

	/* Loop over designs */
	for (i = 0; i < num_design; i++)
	{
		/* Check for card image */
		if (!image_cache[i])
		{
			/* Format error message */
			sprintf(msg, "Error: Could not load card image %3d!\n",
			        i);

			/* Error */
			display_error(msg);
			return -1;
		}
	}

	/* Loop over goals */
	for (i = 0; i < MAX_GOAL; i++)
	{
		/* Check for goal image */
		if (!goal_cache[i])
		{
			/* Format error message */
			sprintf(msg, "Error: Could not load goal image %2d!\n",
			        i);

			/* Error */
			display_error(msg);
			return -1;
		}
	}

	/* Loop over icons */
	for (i = 0; i < MAX_ICON; i++)
	{
		/* Skip second develop/settle action */
		if (i == ACT_DEVELOP2 || i == ACT_SETTLE2) continue;

		/* Check for icon image */
		if (!icon_cache[i])
		{
			/* Format error message */
			sprintf(msg, "Error: Could not load icon image %2d!\n",
			        i);

			/* Error */
			display_error(msg);
			return -1;
		}
	}

	/* Loop over actions */
	for (i = 0; i < MAX_ACT_CARD; i++)
	{
		/* Skip second develop/settle action */
		if (i == ACT_DEVELOP2 || i == ACT_SETTLE2) continue;

		/* Check for card image */
		if (!action_cache[i])
		{
			/* Format error message */
			sprintf(msg,
			       "Error: Could not load action card image %2d!\n",
			       i);

			/* Error */
			display_error(msg);
			return -1;
		}
	}

	/* Success */
	return 0;
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

	/* Loop over players */
	for (i = 0; i < sim.num_players; i++)
	{
		/* Have AI make any pending decisions for this player */
		sim.p[i].control = &ai_func;
	}

	/* Try to make payment */
	return payment_callback(&sim, player_us, action_payment_which,
	                        list, n, special, ns, action_payment_mil,
	                        action_payment_bonus);
}

/*
 * Function to determine whether selected goods can be consumed.
 */
static gboolean action_check_goods(void)
{
	game sim;
	displayed *i_ptr;
	int i, n = 0, multi = -1;
	int list[MAX_DECK];

	/* Loop over cards on table */
	for (i = 0; i < table_size[player_us]; i++)
	{
		/* Get displayed card pointer */
		i_ptr = &table[player_us][i];

		/* Skip unselected */
		if (!i_ptr->selected) continue;

		/* Check for multiple goods */
		if (i_ptr->num_goods > 1) multi = i;

		/* Add to regular list */
		list[n++] = i_ptr->index;
	}

	/* Check for too few and more available */
	if (n < action_min && multi >= 0)
	{
		/* Get displayed card pointer */
		i_ptr = &table[player_us][multi];

		/* Check for not enough available */
		if (n + i_ptr->num_goods - 1 < action_min) return 0;

		/* Add more until enough */
		while (n < action_min) list[n++] = i_ptr->index;
	}

	/* Check for too few */
	if (n < action_min) return 0;

	/* Check for too many */
	if (n > action_max) return 0;

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
 * Set of "extra info" structures for player statuses.
 */
static struct extra_info status_extra_info[MAX_PLAYER][5];

/*
 * Set of "extra info" structures for game status.
 */
static struct extra_info game_extra_info[3];

/*
 * Set of "extra info" structures for selectable cards.
 */
static struct extra_info card_extra_info[MAX_ACCEL];

/*
 * The current number of used accelerator keys.
 */
static int key_count;

/*
 * The first accelerator key for cards in hand.
 */
static int hand_first_key;

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
	int x = 0, y = 0;
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

	/* Check for centered text */
	if (!ei->top_left)
	{
		/* Compute point to start drawing */
		x = (image->allocation.width - tw) / 2 + image->allocation.x;
		y = (image->allocation.height - th) / 2 + image->allocation.y;
	}

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
 * Refresh the full-size card image.
 *
 * Called when the pointer moves over a small card image.
 */
static gboolean redraw_full(GtkWidget *widget, GdkEventCrossing *event,
                            gpointer data)
{
	design *d_ptr = (design *)data;

	/* Update card image */
	update_card(d_ptr ? image_cache[d_ptr->index] : NULL);

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
	/* Update image */
	update_card(action_cache[GPOINTER_TO_INT(data)]);

	/* Continue to handle event */
	return FALSE;
}

/*
 * Create an event box containing the given card's image.
 */
static GtkWidget *new_image_box(design *d_ptr, int w, int h, int color,
                                int highlight, int back, int accel_key)
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
		gdk_pixbuf_saturate_and_pixelate(buf, buf, 0.5, TRUE);
	}

	/* Check for border placed around image */
	if (highlight == HIGH_YELLOW)
	{
		/* Compute border width */
		bw = w / 20;

		/* Enforce minimum border width */
		if (bw < 5) bw = 5;

		/* Create a border pixbuf */
		border_buf = gdk_pixbuf_new(GDK_COLORSPACE_RGB, TRUE, 8, w, h);

		/* Fill pixbuf with highlight color */
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

	/* Check for red discard highlight */
	else if (highlight == HIGH_RED)
	{
		/* Scale discard icon */
		border_buf = gdk_pixbuf_scale_simple(icon_cache[ICON_DISCARD],
		                                     w, h, GDK_INTERP_BILINEAR);

		/* Composite discard symbol onto card image buffer */
		gdk_pixbuf_composite(border_buf, buf, 0, 0, w, h,
		                     0, 0, 1, 1, GDK_INTERP_BILINEAR, 255);

		/* Release our copy of scaled discard icon */
		g_object_unref(G_OBJECT(border_buf));
	}

	/* Make image widget */
	image = gtk_image_new_from_pixbuf(buf);

	/* Check for accelerator key */
	if (accel_key >= 0 && accel_key < MAX_ACCEL)
	{
		/* Connect expose-event to draw extra text */
		g_signal_connect_after(G_OBJECT(image), "expose-event",
		                       G_CALLBACK(draw_extra_text),
		                       &card_extra_info[accel_key]);
	}

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
 * Update sensitivity of the 'done' button.
 */
static void update_action_sensitivity()
{
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
}

/*
 * Card selected/deselected.
 */
static gboolean card_selected(GtkWidget *widget, GdkEventButton *event,
                              gpointer data)
{
	displayed *i_ptr = (displayed *)data;
	displayed *j_ptr;
	int i, j, select_others = -1;

	/* Check for right-click */
	if (event && event->button == 3 && !i_ptr->greedy)
	{
		i_ptr->selected = 0;
		select_others = 0;

		/* Check for hand */
		if (i_ptr->hand)
		{
			/* Loop over other cards in hand */
			for (i = 0; i < hand_size; i++)
			{
				/* Get displayed card pointer */
				j_ptr = &hand[i];

				/* Skip non-eligible cards */
				if (!j_ptr->eligible) continue;

				/* Skip current card */
				if (i_ptr == j_ptr) continue;

				/* Check for deselected card */
				if (!j_ptr->selected)
				{
					/* Remember to select all others */
					select_others = 1;
					break;
				}
			}
		}
		else
		{
			/* Loop over all table areas */
			for (i = 0; i < MAX_PLAYER; i++)
			{
				/* Loop over cards in table area */
				for (j = 0; j < table_size[i]; j++)
				{
					/* Get displayed card pointer */
					j_ptr = &table[i][j];

					/* Skip non-eligible cards */
					if (!j_ptr->eligible) continue;

					/* Skip current card */
					if (i_ptr == j_ptr) continue;

					/* Check for deselected card */
					if (!j_ptr->selected)
					{
						/* Remember to select all others */
						select_others = 1;
						break;
					}
				}
			}
		}
	}
	else
	{
		/* Change selection status */
		i_ptr->selected = !i_ptr->selected;

		/* Check for greedy card */
		if (i_ptr->greedy && i_ptr->selected)
		{
			select_others = 0;
		}
	}

	/* Check for modifying other cards */
	if (select_others >= 0)
	{
		/* Check for hand */
		if (i_ptr->hand)
		{
			/* Loop over other cards in hand */
			for (i = 0; i < hand_size; i++)
			{
				/* Get displayed card pointer */
				j_ptr = &hand[i];

				/* Skip non-eligible cards */
				if (!j_ptr->eligible) continue;

				/* Skip current card */
				if (i_ptr == j_ptr) continue;

				/* Select/deselect card */
				j_ptr->selected = select_others;
			}
		}
		else
		{
			/* Loop over all table areas */
			for (i = 0; i < MAX_PLAYER; i++)
			{
				/* Loop over cards in table area */
				for (j = 0; j < table_size[i]; j++)
				{
					/* Get displayed card pointer */
					j_ptr = &table[i][j];

					/* Skip non-eligible cards */
					if (!j_ptr->eligible) continue;

					/* Skip current card */
					if (i_ptr == j_ptr) continue;

					/* Select/deselect card */
					j_ptr->selected = select_others;
				}
			}
		}
	}

	/* Update done button sensitivity */
	update_action_sensitivity();

	/* Check for card in hand and no eligible cards on table */
	if (i_ptr->hand && key_count == 0)
	{
		/* Redraw hand */
		redraw_hand();
	}
	else
	{
		/* Redraw table and hand */
		redraw_table();
		redraw_hand();
	}

	/* Event handled */
	return TRUE;
}

/*
 * Card selected by keypress.
 */
static void card_keyed(GtkWidget *widget, gpointer data)
{
	/* Mark accelerator key used */
	accel_used = TRUE;

	/* Call regular handler */
	card_selected(widget, NULL, data);
}

/*
 * Select/deselect all cards by keypress.
 */
static void card_select_all(GtkWidget *widget, gpointer data)
{
	displayed *i_ptr;
	int i, select_all = GPOINTER_TO_INT(data);

	/* Mark accelerator key used */
	accel_used = TRUE;

	/* Loop over cards in hand */
	for (i = 0; i < hand_size; i++)
	{
		/* Get displayed card pointer */
		i_ptr = &hand[i];

		/* Skip non-eligible cards */
		if (!i_ptr->eligible) continue;

		/* Select/deselect card */
		i_ptr->selected = select_all;
	}

	/* Update done button sensitivity */
	update_action_sensitivity();

	/* Check for no eligible cards on table */
	if (key_count == 0)
	{
		/* Redraw hand */
		redraw_hand();
	}
	else
	{
		/* Redraw table (to add key reminders) and hand */
		redraw_table();
		redraw_hand();
	}
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
 * Function to compare two cards in the hand for sorting.
 */
static int cmp_hand(const void *h1, const void *h2)
{
	displayed *i_ptr1 = (displayed *)h1, *i_ptr2 = (displayed *)h2;
	card *c_ptr1, *c_ptr2;

	/* Get card pointers */
	c_ptr1 = &real_game.deck[i_ptr1->index];
	c_ptr2 = &real_game.deck[i_ptr2->index];

	/* Gapped cards always go after non-gapped */
	if (i_ptr1->gapped && !i_ptr2->gapped) return 1;
	if (!i_ptr1->gapped && i_ptr2->gapped) return -1;

	/* Worlds come before developments */
	if (c_ptr1->d_ptr->type != c_ptr2->d_ptr->type)
	{
		/* Check for development */
		if (c_ptr1->d_ptr->type == TYPE_DEVELOPMENT) return 1;
		if (c_ptr2->d_ptr->type == TYPE_DEVELOPMENT) return -1;
	}

	/* Sort by cost */
	if (c_ptr1->d_ptr->cost != c_ptr2->d_ptr->cost)
	{
		/* Return cost difference */
		return c_ptr1->d_ptr->cost - c_ptr2->d_ptr->cost;
	}

	/* Otherwise sort by index */
	return i_ptr1->index - i_ptr2->index;
}

/*
 * Redraw hand area.
 */
void redraw_hand(void)
{
	GtkWidget *box;
	displayed *i_ptr;
	int count = 0, gap = 1, n, num_gap = 0;
	int width, height, highlight;
	int card_w, card_h;
	int i, j, select_all_added = FALSE;

	/* Check if hand previously drawn */
	if (hand_first_key != -1)
	{
		/* Reset key count */
		key_count = hand_first_key;
	}
	else
	{
		/* Save key count */
		hand_first_key = key_count;
	}

	/* Sort hand */
	qsort(hand, hand_size, sizeof(displayed), cmp_hand);

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
	card_w = width / 8;

	/* Compute height of card */
	card_h = card_w * CARD_HEIGHT / CARD_WIDTH;

	/* Compute pixels per card */
	if (n > 0) width = width / n;

	/* Maximum width */
	if (width > card_w * 1.1) width = card_w * 1.1;

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

		/* Assume no highlighting */
		highlight = HIGH_NONE;

		/* Check for selected */
		if (i_ptr->selected)
		{
			/* Use given highlight color */
			highlight = i_ptr->highlight;
		}
		else
		{
			/* Check for other card selected */
			for (j = 0; j < hand_size; j++)
			{
				/* Skip current card */
				if (i == j) continue;

				/* Check for selected */
				if (hand[j].selected)
				{
					/* Use alternate highlight color */
					highlight = i_ptr->highlight_else;
				}
			}
		}

		/* Get event box with image */
		box = new_image_box(i_ptr->d_ptr, card_w, card_h,
		                    i_ptr->eligible || i_ptr->color,
		                    highlight, 0,
		                    i_ptr->eligible && (accel_used || opt.key_cues) ?
		                                       key_count : -1);

		/* Place event box */
		gtk_fixed_put(GTK_FIXED(hand_area), box, count * width,
		              i_ptr->selected && i_ptr->push ? 0 :
		                                             height - card_h);

		/* Check for eligible card */
		if (i_ptr->eligible)
		{
			/* Connect "button released" signal */
			g_signal_connect(G_OBJECT(box), "button-release-event",
			                 G_CALLBACK(card_selected), i_ptr);

			/* Check for not greedy and not select-all added */
			if (!i_ptr->greedy && !select_all_added)
			{
				/* Add key handler for select all */
				gtk_widget_add_accelerator(box, "key-select-all",
				                           window_accel,
				                           GDK_F12, 0, 0);

				/* Connect key-select-all */
				g_signal_connect(G_OBJECT(box), "key-select-all",
				                 G_CALLBACK(card_select_all),
				                 GINT_TO_POINTER(1));

				/* Add key handler for deselect all */
				gtk_widget_add_accelerator(box, "key-deselect-all",
				                           window_accel,
				                           GDK_F12, GDK_SHIFT_MASK, 0);

				/* Connect key-deselect-all */
				g_signal_connect(G_OBJECT(box), "key-deselect-all",
				                 G_CALLBACK(card_select_all),
				                 GINT_TO_POINTER(0));

				/* Remember event is added */
				select_all_added = TRUE;
			}

			/* Check for enough accelerator keys */
			if (key_count < MAX_ACCEL)
			{
				/* Add handler for keypresses */
				gtk_widget_add_accelerator(box,
				                           "key-signal",
				                           window_accel,
				                           accel_keys[key_count],
				                           accel_mods[key_count],
				                           0);

				/* Check if client is disconnected and enough numeric keys */
				if (client_state == CS_DISCONN && key_count < 9)
				{
					/* Add numeric key handler */
					gtk_widget_add_accelerator(box, "key-signal",
					                           window_accel,
					                           GDK_1 + key_count, 0, 0);
				}

				/* Connect key-signal */
				g_signal_connect(G_OBJECT(box), "key-signal",
				                 G_CALLBACK(card_keyed),
				                 i_ptr);

				/* Increment count */
				key_count++;
			}
		}

		/* Add tooltip if available */
		if (i_ptr->tooltip)
		{
			/* Add tooltip to widget */
			gtk_widget_set_tooltip_text(box, i_ptr->tooltip);
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
	int width, height, padding, highlight;
	int card_w, card_h;
	int i, j, n;

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

	/* Compute amount of padding between cards */
	padding = (width / 20) / (col - 1);

	/* Get width of individual card */
	card_w = (width - (col - 1) * padding) / col;

	/* Compute height of card */
	card_h = card_w * CARD_HEIGHT / CARD_WIDTH;

	/* Height of row */
	height = height / row;

	/* Maximum height */
	if (height > card_h * 1.1) height = card_h * 1.1;

	/* Width is card width */
	width = card_w + padding;

	/* Loop over cards */
	for (i = 0; i < table_size[who]; i++)
	{
		/* Get displayed card pointer */
		i_ptr = &table[who][i];

		/* Assume no highlighting */
		highlight = HIGH_NONE;

		/* Check for selected */
		if (i_ptr->selected)
		{
			/* Use given highlight color */
			highlight = i_ptr->highlight;
		}
		else
		{
			/* Check for other card selected */
			for (j = 0; j < table_size[who]; j++)
			{
				/* Skip current card */
				if (i == j) continue;

				/* Check for selected */
				if (table[who][j].selected)
				{
					/* Use alternate highlight color */
					highlight = i_ptr->highlight_else;
				}
			}
		}

		/* Get event box with image */
		box = new_image_box(i_ptr->d_ptr, card_w, card_h,
		                    i_ptr->eligible || i_ptr->color,
		                    highlight, 0,
		                    i_ptr->eligible && (accel_used || opt.key_cues) ?
		                                       key_count : -1);

		/* Place event box */
		gtk_fixed_put(GTK_FIXED(area), box, x * width, y * height);

		/* Show image */
		gtk_widget_show_all(box);

		/* Add tooltip if available */
		if (i_ptr->tooltip)
		{
			/* Add tooltip to widget */
			gtk_widget_set_tooltip_text(box, i_ptr->tooltip);
		}

		/* Check for good */
		if (i_ptr->num_goods || (i_ptr->selected && i_ptr->push))
		{
			/* Get event box with no image */
			good_box = new_image_box(i_ptr->d_ptr,
			                         3 * card_w / 4, 3 * card_h / 4,
			                         i_ptr->eligible || i_ptr->color, 0, 1, -1);

			/* Place box on card */
			gtk_fixed_put(GTK_FIXED(area), good_box,
			              x * width + card_w / 4,
			              i_ptr->selected && i_ptr->push ?
			                  y * height :
				              y * height + card_h / 4);

			/* Add tooltip if available */
			if (i_ptr->tooltip)
			{
				/* Add tooltip to widget */
				gtk_widget_set_tooltip_text(good_box, i_ptr->tooltip);
			}

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

			/* Check for enough accelerator keys */
			if (key_count < MAX_ACCEL)
			{
				/* Add handler for keypresses */
				gtk_widget_add_accelerator(box,
				                           "key-signal",
				                           window_accel,
				                           accel_keys[key_count],
				                           accel_mods[key_count],
				                           0);

				/* Check if client is disconnected and enough numeric keys */
				if (client_state == CS_DISCONN && key_count < 9)
				{
					/* Add numeric key handler */
					gtk_widget_add_accelerator(box, "key-signal",
					                           window_accel,
					                           GDK_1 + key_count, 0, 0);
				}

				/* Connect key-signal */
				g_signal_connect(G_OBJECT(box), "key-signal",
				                 G_CALLBACK(card_keyed),
				                 i_ptr);

				/* Increment count */
				key_count++;
			}
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
	int i, n;

	/* Reset accelerator keys */
	key_count = 0;
	hand_first_key = -1;

	/* Loop over players */
	for (i = 0; i < real_game.num_players; i++)
	{
		/* Compute index of player to redraw */
		n = (player_us + i + 1) % real_game.num_players;

		/* Redraw player area */
		redraw_table_area(n, player_area[n]);
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
	sprintf(msg, "%s\n(%s)", goal_name[goal], goal_description[goal]);

	/* Check for first goal */
	if (goal <= GOAL_FIRST_4_MILITARY)
	{
		/* Check for claimed goal */
		if (!g->goal_avail[goal])
		{
			/* Add text to tooltip */
			strcat(msg, "\n\nClaimed by:");

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

		/* Report progress for one-dimensional first goals */
		/* (if local or server supports it) */
		else if (goal_minimum(goal) > 1 &&
		         (client_state == CS_DISCONN || strlen(server_version)))
		{
			/* Add text to tooltip */
			strcat(msg, "\n\nProgress:");

			/* Loop over players */
			for (i = 0; i < g->num_players; i++)
			{
				/* Get player pointer */
				p_ptr = &g->p[i];

				/* Create progress string */
				sprintf(text, "\n %s: %d",
				        p_ptr->name, p_ptr->goal_progress[goal]);

				/* Add progress string to tooltip */
				strcat(msg, text);
			}
		}
	}

	/* Check for most goal */
	if (goal >= GOAL_MOST_MILITARY)
	{
		/* Add text to tooltip */
		strcat(msg, "\n\nProgress:");

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
	int i;
	int width, height, goal_h, y = 0;

	/* First destroy all pre-existing goal widgets */
	gtk_container_foreach(GTK_CONTAINER(goal_area), destroy_widget, NULL);

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
 * Create a tooltip for vp icon.
 */
static char *get_vp_tooltip(game *g, int who)
{
	static char msg[1024];
	player *p_ptr = &g->p[who];
	card *c_ptr;
	char text[1024];
	char bonus[1024];
	int x, t, kind, worlds, devs;

	/* Clear counts */
	worlds = devs = 0;

	/* Clear messages */
	strcpy(msg, "");
	strcpy(bonus, "");

	/* Check for VP from chips */
	if (p_ptr->vp)
	{
		/* Count VP chips */
		sprintf(text, "\nVP chips: %d VP%s", p_ptr->vp, PLURAL(p_ptr->vp));
		strcat(msg, text);
	}

	/* Check for VP from goals */
	if (p_ptr->goal_vp)
	{
		/* Count goal VP */
		sprintf(text, "\nGoals: %d VP%s", p_ptr->goal_vp,
		        PLURAL(p_ptr->goal_vp));
		strcat(msg, text);
	}

	/* Check for VP from prestige */
	if (p_ptr->prestige)
	{
		/* Count goal VP */
		sprintf(text, "\nPrestige: %d VP%s", p_ptr->prestige,
		        PLURAL(p_ptr->prestige));
		strcat(msg, text);
	}

	/* Remember old kind */
	kind = g->oort_kind;

	/* Set oort kind to best scoring kind */
	g->oort_kind = g->best_oort_kind;

	/* Start at first active card */
	x = p_ptr->head[WHERE_ACTIVE];

	/* Loop over active cards */
	for ( ; x != -1; x = g->deck[x].next)
	{
		/* Get card pointer */
		c_ptr = &g->deck[x];

		/* Check for world */
		if (c_ptr->d_ptr->type == TYPE_WORLD)
		{
			/* Add VP from this world */
			worlds += c_ptr->d_ptr->vp;
		}

		/* Check for development */
		else if (c_ptr->d_ptr->type == TYPE_DEVELOPMENT)
		{
			/* Add VP from this development */
			devs += c_ptr->d_ptr->vp;
		}

		/* Check for VP bonuses */
		if (c_ptr->d_ptr->num_vp_bonus)
		{
			/* Count VPs from this card */
			t = get_score_bonus(g, who, x);

			/* Copy previous bonus (to get names in table order) */
			strcpy(text, bonus);

			/* Format text */
			sprintf(bonus, "\n%s: %d VP%s", c_ptr->d_ptr->name, t, PLURAL(t));

			/* Add to bonus string */
			strcat(bonus, text);
		}
	}

	/* Reset oort kind */
	g->oort_kind = kind;

	/* Check for VP from worlds */
	if (worlds)
	{
		/* Add total count from worlds */
		sprintf(text, "\nWorlds: %d VP%s", worlds, PLURAL(worlds));
		strcat(msg, text);
	}

	/* Check for VP from developments */
	if (devs)
	{
		/* Add total count from developments */
		sprintf(text, "\nDevelopments: %d VP%s", devs, PLURAL(devs));
		strcat(msg, text);
	}

	/* Add bonus cards */
	strcat(msg, bonus);

	/* Write total */
	sprintf(text, "\nTotal: <b>%d VP%s</b>", p_ptr->end_vp,
	        PLURAL(p_ptr->end_vp));
	strcat(msg, text);

	/* Return message (without first newline) */
	return msg + 1;
}

/*
 * Create a tooltip for a discount icon.
 */
static char *get_discount_tooltip(discounts *discount)
{
	static char msg[1024];
	char text[1024];
	int i;

	/* Clear text */
	strcpy(msg, "");

	/* Compute discounts */
	if (!discount->has_data)
	{
		/* Return empty message */
		return msg;
	}

	/* Add general discount */
	sprintf(text, "Base discount: -%d", discount->base);
	strcat(msg, text);

	/* Add bonus discount */
	if (discount->bonus)
	{
		sprintf(text, "\nAdditional bonus discount: -%d", discount->bonus);
		strcat(msg, text);
	}

	/* Add specific discounts */
	for (i = GOOD_NOVELTY; i <= GOOD_ALIEN; ++i)
	{
		/* Check for discount */
		if (discount->specific[i])
		{
			/* Create text */
			sprintf(text, "\nAdditional %s discount: %d",
			        good_printable[i], -discount->specific[i]);
			strcat(msg, text);
		}
	}

	/* Add pay for non-alien military with discount */
	if (discount->non_alien_mil_1 ||
	    (discount->non_alien_mil_0 && discount->pay_discount))
	{
		/* Create text */
		sprintf(text, "\nAdditional discount when paying\n"
		        "  for non-Alien military worlds: -%d",
		        (discount->non_alien_mil_1 ? 1 : 0) + discount->pay_discount);
		strcat(msg, text);
	}

	/* Add pay for rebel military with discount */
	if (discount->rebel_mil_2)
	{
		/* Create text */
		sprintf(text, "\nAdditional discount when paying\n"
		        "  for Rebel military worlds: -%d", 2 + discount->pay_discount);
		strcat(msg, text);
	}

	/* Check for discount when paying for military */
	if (discount->pay_discount)
	{
		/* Add pay for chromo military with discount */
		if (discount->chromo_mil)
		{
			/* Create text */
			sprintf(text, "\nAdditional discount when paying\n"
			        "  for worlds with a Chromosome symbol: -%d",
			        discount->pay_discount);
			strcat(msg, text);
		}

		/* Add pay for alien military with discount */
		if (discount->alien_mil)
		{
			/* Create text */
			sprintf(text, "\nAdditional discount when paying\n"
			        "  for Alien military worlds: -%d", discount->pay_discount);
			strcat(msg, text);
		}
	}

	/* No pay-for-military discount */
	else
	{
		/* Add pay for non-alien military without discount */
		if (discount->non_alien_mil_0 && !discount->non_alien_mil_1)
		{
			/* Create text */
			strcat(msg, "\nMay pay to settle a non-Alien military world");
		}

		/* Add pay for chromo military without discount */
		if (discount->chromo_mil)
		{
			/* Create text */
			strcat(msg, "\nMay pay to settle a military world\n"
				   "  with a Chromosome symbol");
		}

		/* Add pay for alien military without discount */
		if (discount->alien_mil)
		{
			/* Create text */
			strcat(msg, "\nMay pay to settle an Alien military world");
		}
	}

	/* Add discard to zero */
	if (discount->zero)
	{
		/* Create text */
		strcat(msg, "\nMay discard to place at 0 cost");
	}

	/* Add discard to conquer without discount */
	if (discount->conquer_settle_0)
	{
		/* Create text */
		strcat(msg, "\nMay discard to conquer a non-military world\n"
		       "  (defense = cost)");
	}

	/* Add discard to conquer with discount */
	if (discount->conquer_settle_2)
	{
		/* Create text */
		strcat(msg, "\nMay discard to conquer a non-military world\n"
		       "  (defense = cost - 2)");
	}

	/* Return tooltip text */
	return msg;
}

/*
 * Create a tooltip for a military strength icon.
 */
static char *get_military_tooltip(mil_strength *military)
{
	static char msg[1024];
	char text[1024];
	int i;

	/* Clear text */
	strcpy(msg, "");

	/* Check for values */
	if (!military->has_data)
	{
		/* Return empty message */
		return msg;
	}

	/* Add base strength */
	sprintf(text, "Base strength: %+d", military->base);
	strcat(msg, text);

	/* Add temporary military */
	if (military->bonus)
	{
		/* Create text */
		sprintf(text, "\nActivated temporary military: %+d",
		        military->bonus);
		strcat(msg, text);
	}

	/* Add rebel strength */
	if (military->rebel)
	{
		/* Create rebel text */
		sprintf(text, "\nAdditional Rebel strength: %+d", military->rebel);
		strcat(msg, text);
	}

	/* Add specific strength */
	for (i = GOOD_NOVELTY; i <= GOOD_ALIEN; ++i)
	{
		/* Check for strength */
		if (military->specific[i])
		{
			/* Create text */
			sprintf(text, "\nAdditional %s strength: %+d",
			        good_printable[i], military->specific[i]);
			strcat(msg, text);
		}
	}

	/* Add defense strength */
	if (military->defense)
	{
		/* Create text */
		sprintf(text, "\nAdditional Takeover defense: %+d", military->defense);
		strcat(msg, text);
	}

	/* Add attack imperium */
	if (military->attack_imperium)
	{
		/* Create text */
		sprintf(text, "\nAdditional attack when using %s: %+d",
		        military->imp_card, military->attack_imperium);
		strcat(msg, text);
	}

	/* Add maximum temporary military */
	if (military->max_bonus)
	{
		/* Create text */
		sprintf(text, "\nAdditional potential temporary military: %+d", military->max_bonus);
		strcat(msg, text);
	}

	/* Check for active imperium card */
	if (military->imperium)
	{
		/* Add vulnerability text */
		strcat(msg, "\nIMPERIUM card played");
	}

	/* Check for active Rebel military world */
	if (military->military_rebel)
	{
		/* Add vulnerability text */
		strcat(msg, "\nREBEL Military world played");
	}

	/* Return tooltip text */
	return msg;
}

/*
 * Create a tooltip for the Prestige icon.
 */
static char *get_prestige_tooltip(game *g, int who)
{
	static char msg[1024];

	/* Do nothing unless third expansion is present */
	if (g->expanded != 3) return "";

	/* Create text */
	sprintf(msg, "Prestige/Search action used: <b>%s</b> ",
	             (g->p[who].prestige_action_used ? "YES" : "NO"));

	/* Check if player has prestige on tile */
	if (prestige_on_tile(g, who))
	{
		/* Append to message */
		strcat(msg, "\nPrestige is on the tile");
	}

	/* Return tooltip text */
	return msg;
}

/*
 * Create a tooltip for a card displayed in the player's hand.
 */
static char *card_hand_tooltip(game *g, int who, int which)
{
	char text[1024];
	card *c_ptr;
	power_where w_list[100];
	power *o_ptr;
	int n, i, old_vp, vp_diff, goal_diff = 0, goal_action;
	game sim;

	/* Copy game */
	sim = *g;

	/* Set simulated game */
	sim.simulation = 1;

	/* Simulate end of phase (for cards already placed) */
	clear_temp(&sim);

	/* Apply goals */
	check_goals(&sim);

	/* Score game for player */
	score_game(&sim);

	/* Remember old score */
	old_vp = sim.p[who].end_vp;

	/* Simulate placement of card */
	place_card(&sim, who, which);

	/* Get card pointer */
	c_ptr = &sim.deck[which];

	/* Check for development type */
	if (c_ptr->d_ptr->type == TYPE_DEVELOPMENT)
	{
		/* Get list of develop powers */
		n = get_powers(&sim, who, PHASE_DEVELOP, w_list);

		/* Loop over powers */
		for (i = 0; i < n; i++)
		{
			/* Get power pointer */
			o_ptr = w_list[i].o_ptr;

			/* Check for "earn prestige after Rebel" power */
			if (o_ptr->code & P2_PRESTIGE_REBEL)
			{
				/* Check for Rebel flag on played card */
				if (c_ptr->d_ptr->flags & FLAG_REBEL)
				{
					/* Reward prestige */
					sim.p[who].prestige += o_ptr->value;
				}
			}

			/* Check for "earn prestige after 6 dev" power */
			if (o_ptr->code & P2_PRESTIGE_SIX)
			{
				/* Check for six-cost development */
				if (c_ptr->d_ptr->cost == 6)
				{
					/* Reward prestige */
					sim.p[who].prestige += o_ptr->value;
				}
			}

			/* Check for "earn prestige" power */
			if (o_ptr->code & P2_PRESTIGE)
			{
				/* Reward prestige */
				sim.p[who].prestige += o_ptr->value;
			}
		}

		/* Check goals for develop phase */
		goal_action = ACT_DEVELOP;
	}

	/* World type */
	else
	{
		/* Get settle phase powers */
		n = get_powers(g, who, PHASE_SETTLE, w_list);

		/* Loop over pre-existing powers */
		for (i = 0; i < n; i++)
		{
			/* Get power pointer */
			o_ptr = w_list[i].o_ptr;

			/* Check for prestige after rebel power */
			if (o_ptr->code & P3_PRESTIGE_REBEL)
			{
				/* Check for rebel military world placed */
				if ((c_ptr->d_ptr->flags & FLAG_REBEL) &&
				    (c_ptr->d_ptr->flags & FLAG_MILITARY))
				{
					/* Reward prestige */
					sim.p[who].prestige += o_ptr->value;
				}
			}

			/* Check for prestige after production world */
			if (o_ptr->code & P3_PRODUCE_PRESTIGE)
			{
				/* Check for production world */
				if (c_ptr->d_ptr->good_type > 0 &&
				    !(c_ptr->d_ptr->flags & FLAG_WINDFALL))
				{
					/* Reward prestige */
					sim.p[who].prestige += o_ptr->value;
				}
			}

			/* Check for "auto-production" */
			if (o_ptr->code & P3_AUTO_PRODUCE)
			{
				/* Check for production world placed */
				if (c_ptr->d_ptr->good_type > 0 &&
				    !(c_ptr->d_ptr->flags & FLAG_WINDFALL))
				{
					/* Add good to world */
					add_good(&sim, which);
				}
			}
		}

		/* Check goals for settle phase */
		goal_action = ACT_SETTLE;
	}

	/* Simulate end of phase (for self-scoring cards) */
	clear_temp(&sim);

	/* Score game for player */
	score_game(&sim);

	/* Compute score difference */
	vp_diff = sim.p[who].end_vp - old_vp;

	/* Check for goals */
	if (goals_enabled(g))
	{
		/* Set placement phase */
		sim.cur_action = goal_action;

		/* Apply goals */
		check_goals(&sim);

		/* Score game for player */
		score_game(&sim);

		/* Compute extra goal points */
		goal_diff = sim.p[who].end_vp - old_vp - vp_diff;
	}

	/* Check for points from goals */
	if (goal_diff)
	{
		/* Format message */
		sprintf(text, "%d VP%s\nGoals: %+d VP%s", vp_diff, PLURAL(vp_diff),
		        goal_diff, PLURAL(goal_diff));
	}
	else
	{
		/* Format message */
		sprintf(text, "%d VP%s", vp_diff, PLURAL(vp_diff));
	}

	/* Return tool tip */
	return strdup(text);
}

/*
 * Create a tooltip for a card displayed on a table.
 */
static char *card_table_tooltip(game *g, int who, int which)
{
	char text[1024];
	card *c_ptr, *b_ptr;
	int i, vp, kind, count = 0;

	/* Get card pointer */
	c_ptr = &g->deck[which];

	/* Check for cards saved */
	if (c_ptr->d_ptr->flags & FLAG_START_SAVE)
	{
		/* Loop over cards in deck */
		for (i = 0; i < g->deck_size; i++)
		{
			/* Get card pointer */
			b_ptr = &g->deck[i];

			/* Count cards saved */
			if (b_ptr->where == WHERE_SAVED) count++;
		}

		/* Start tooltip text */
		sprintf(text, "%d card%s saved", count, PLURAL(count));

		/* Check for card not owned by player showing */
		if (count == 0 || c_ptr->owner != who) return strdup(text);

		/* Add list of cards saved */
		strcat(text, ":\n");

		/* Loop over cards in deck */
		for (i = 0; i < g->deck_size; i++)
		{
			/* Get card pointer */
			b_ptr = &g->deck[i];

			/* Skip cards that aren't saved */
			if (b_ptr->where != WHERE_SAVED) continue;

			/* Add card name to tooltip */
			strcat(text, "\n\t");
			strcat(text, b_ptr->d_ptr->name);
		}

		/* Return tooltip */
		return strdup(text);
	}

	/* Check for multiple goods */
	else if (c_ptr->num_goods >= 2)
	{
		/* Set text */
		sprintf(text, "%d goods", c_ptr->num_goods);

		/* Return tooltip */
		return strdup(text);
	}

	/* Check for vp bonuses */
	else if (c_ptr->d_ptr->num_vp_bonus > 0)
	{
		/* Remember old kind */
		kind = g->oort_kind;

		/* Set oort kind to best scoring kind */
		g->oort_kind = g->best_oort_kind;

		/* Compute VPs */
		vp = get_score_bonus(g, c_ptr->owner, which);

		/* Reset oort kind */
		g->oort_kind = kind;

		/* Format tooltip text */
		sprintf(text, "%d VP%s", vp, PLURAL(vp));

		/* Return tooltip */
		return strdup(text);
	}

	/* Nothing to display */
	return NULL;
}

/*
 * Create a tooltip for a development that can be placed.
 */
static char *card_develop_tooltip(game *g, int who, displayed *i_ptr)
{
	char text[1024], *p;
	int cost;

	/* Set text pointer */
	p = text;

	/* Check for old tool tip */
	if (i_ptr->tooltip)
	{
		/* Keep previous tool tip */
		strcpy(p, i_ptr->tooltip);

		/* Advance text pointer */
		p += strlen(p);

		/* Add newline */
		p += sprintf(p, "\n");

		/* Free old tool tip */
		free(i_ptr->tooltip);
	}

	/* Compute cost */
	cost = devel_cost(g, who, i_ptr->index);

	/* Add cost */
	p += sprintf(p, "Cost to place: %d", cost);

	/* Return the text */
	return strdup(text);
}

/*
 * Compute the military/cost needed for a military world.
 */
static void military_world_payment(game *g, int who, int which,
                                   int mil_only, int mil_bonus, discounts *d_ptr,
                                   int *military, int *cost, char **cost_card)
{
	card *c_ptr;
	int strength, pay_for_mil;

	/* Get card */
	c_ptr = &g->deck[which];

	/* Get current strength */
	strength = strength_against(g, who, which, -1, 0) + mil_bonus;

	/* Compute extra military needed */
	*military = c_ptr->d_ptr->cost - strength;

	/* Do not reduce below 0 */
	if (*military <= 0) *military = 0;

	/* Reset cost and pay-for-military */
	pay_for_mil = *cost = -1;

	/* Check for no pay-for-military available */
	if (mil_only) return;

	/* Check for Rebel Alliance */
	if (d_ptr->rebel_mil_2 && (c_ptr->d_ptr->flags & FLAG_REBEL))
	{
		/* Set reduction to 2 */
		pay_for_mil = 2;

		/* Save card name */
		*cost_card = "Rebel Alliance";
	}

	/* Check for Contact Specialist */
	else if (d_ptr->non_alien_mil_1 &&
	         c_ptr->d_ptr->good_type != GOOD_ALIEN)
	{
		/* Set reduction to 1 */
		pay_for_mil = 1;

		/* Save card name */
		*cost_card = "Contact Specialist";
	}

	/* Check for Rebel Cantina */
	else if (d_ptr->non_alien_mil_0 &&
	         c_ptr->d_ptr->good_type != GOOD_ALIEN)
	{
		/* Set reduction to 0 */
		pay_for_mil = 0;

		/* Save card name */
		*cost_card = "Rebel Cantina";
	}

	/* Check for Alien Research Team */
	else if (d_ptr->alien_mil &&
	         c_ptr->d_ptr->good_type == GOOD_ALIEN)
	{
		/* Set reduction to 0 */
		pay_for_mil = 0;

		/* Save card name */
		*cost_card = "Alien Research Team";
	}

	/* Check for Ravaged Uplift World */
	else if (d_ptr->chromo_mil && c_ptr->d_ptr->flags & FLAG_CHROMO)
	{
		/* Set reduction to 0 */
		pay_for_mil = 0;

		/* Save card name */
		*cost_card = "Ravaged Uplift World";
	}

	/* Check for any pay-for-military power */
	if (pay_for_mil >= 0)
	{
		/* Compute cost */
		*cost = c_ptr->d_ptr->cost - d_ptr->base - d_ptr->bonus -
		        d_ptr->specific[c_ptr->d_ptr->good_type] -
		        pay_for_mil - d_ptr->pay_discount;

		/* Do not reduce below 0 */
		if (*cost < 0) *cost = 0;
	}
}

/*
 * Compute the cost/military needed for a non-military world.
 */
static void peaceful_world_payment(game *g, int who, int which,
                                   int mil_only, discounts *d_ptr,
                                   int *cost, int *ict_mil, int *iif_mil)
{
	card *c_ptr;
	int strength;

	/* Get card */
	c_ptr = &g->deck[which];

	/* Check for no normal payment available */
	if (mil_only)
	{
		/* Disable payment */
		*cost = -1;
	}
	else
	{
		/* Compute cost */
		*cost = c_ptr->d_ptr->cost - d_ptr->base - d_ptr->bonus -
				d_ptr->specific[c_ptr->d_ptr->good_type];

		/* Do not reduce below 0 */
		if (*cost < 0) *cost = 0;
	}

	/* Compute strength */
	strength = strength_against(g, who, which, -1, 0);

	/* Reset ICT/IIF military */
	*ict_mil = *iif_mil = -1;

	/* Check for Imperium Cloaking Technology */
	if (d_ptr->conquer_settle_2)
	{
		/* Compute extra military needed */
		*ict_mil = c_ptr->d_ptr->cost - strength - 2;

		/* Do not reduce below 0 */
		if (*ict_mil < 0) *ict_mil = 0;
	}

	/* Check for Imperium Invasion Fleet */
	if (d_ptr->conquer_settle_0)
	{
		/* Compute extra military needed */
		*iif_mil = c_ptr->d_ptr->cost - strength;

		/* Do not reduce below 0 */
		if (*iif_mil < 0) *iif_mil = 0;
	}
}
/*
 * Create a tooltip for a world that can be placed.
 */
static char *card_settle_tooltip(game *g, int who, int special, displayed *i_ptr)
{
	card *c_ptr;
	discounts *d_ptr;
	mil_strength *m_ptr;
	char text[1024], *p, *cost_card;
	int which, mil_only, mil_needed, ict_mil, iif_mil, cost, zero_cost;
	int mil_bonus = 0, placed;

	/* Get discounts */
	d_ptr = &status_player[who].discount;

	/* Get military */
	m_ptr = &status_player[who].military;

	/* Set text pointer */
	p = text;

	/* Check for old tool tip */
	if (i_ptr->tooltip)
	{
		/* Keep previous tool tip */
		strcpy(p, i_ptr->tooltip);

		/* Advance text pointer */
		p += strlen(p);

		/* Add newline */
		p += sprintf(p, "\n");

		/* Free old tool tip */
		free(i_ptr->tooltip);
	}

	/* Get card */
	which = i_ptr->index;
	c_ptr = &g->deck[which];

	/* XXX Check for no pay-for-military available */
	mil_only = special >= 0 &&
	           !strcmp(g->deck[special].d_ptr->name, "Rebel Sneak Attack");

	/* XXX Check for zero cost */
	zero_cost = special >= 0 &&
	            !strcmp(g->deck[special].d_ptr->name, "Terraforming Project");

	/* Check for military world */
	if (c_ptr->d_ptr->flags & FLAG_MILITARY)
	{
		/* XXX Check for using extra military */
		if (special >= 0 &&
		    !strcmp(g->deck[special].d_ptr->name, "Imperium Supply Convoy"))
		{
			/* Look up the first world placed */
			placed = g->p[who].head[WHERE_ACTIVE];

			/* Compute strength used for first world */
			mil_bonus = g->deck[placed].d_ptr->cost - strength_first(g, who, g->p[who].head[WHERE_ACTIVE], which);
		}

		/* Compute payment */
		military_world_payment(g, who, which, mil_only, -mil_bonus,
		                       d_ptr, &mil_needed, &cost, &cost_card);

		/* Check for no extra military */
		if (mil_needed <= 0)
		{
			/* Format text */
			p += sprintf(p, "No extra military needed to place\n");
		}
		else
		{
			/* Format text */
			p += sprintf(p, "Extra military needed to place: %d\n",
			             mil_needed);
		}

		/* Check for any pay-for-military power */
		if (cost >= 0)
		{
			/* Format text */
			p += sprintf(p, "Cost to place if using %s: %d\n",
			             cost_card, cost);
		}
	}
	else if (!zero_cost)
	{
		/* Compute peaceful payment */
		peaceful_world_payment(g, who, which, mil_only, d_ptr,
		                       &cost, &ict_mil, &iif_mil);

		/* Check for normal payment available */
		if (cost >= 0)
		{
			/* Format text */
			p += sprintf(p, "Cost to place: %d\n", cost);
		}

		/* Check for Imperium Cloaking Technology */
		if (ict_mil >= 0)
		{
			/* Check for no extra military */
			if (ict_mil == 0)
			{
				/* Format text */
				p += sprintf(p, "No extra military needed to place\n  if using "
				             "Imperium Cloaking Technology\n");
			}
			else
			{
				/* Format text */
				p += sprintf(p, "Extra military needed to place\n  if using "
				             "Imperium Cloaking Technology: %+d\n", ict_mil);
			}
		}

		/* Check for Imperium Invasion Fleet */
		if (iif_mil >= 0)
		{
			/* Check for no extra military */
			if (iif_mil == 0)
			{
				/* Format text */
				p += sprintf(p, "No extra military needed to place\n  if using "
				             "Imperium Invasion Fleet\n");
			}
			else
			{
				/* Format text */
				p += sprintf(p, "Extra military needed to place\n  if using "
				             "Imperium Invasion Fleet: %+d\n", iif_mil);
			}
		}
	}

	/* Trim last newline */
	text[strlen(text) - 1] = '\0';

	/* Return the text */
	return strdup(text);
}

/*
 * Information about a pending takeover.
 */
typedef struct takeover_info
{
	/* The card to be used when attacking */
	int card;

	/* The current attack strength */
	int attack;

	/* The VP diff (including goals) */
	int vp_diff[2];

} takeover_info;

/*
 * Create a tooltip for a card displayed on a table
 * which is eligible for takeover.
 */
static char *card_takeover_tooltip(game *g, int defender, int attacker,
                                   displayed *i_ptr)
{
	char text[1024], *p;
	card *c_ptr;
	design *d_ptr;
	displayed *j_ptr;
	takeover_info takeovers[10], *t_ptr;
	int num_takeovers = 0, total_takeovers = 0;
	int i, j, k, which, defense, old_vp[2];
	game sim;

	/* Get card index */
	which = i_ptr->index;

	/* Get card pointer */
	c_ptr = &g->deck[which];

	/* Compute defense */
	defense = strength_against(g, defender, which, -1, 1);

	/* Copy game */
	sim = *g;

	/* Set simulated game */
	sim.simulation = 1;

	/* Simulate end of phase (for cards already placed) */
	clear_temp(&sim);

	/* Apply goals */
	check_goals(&sim);

	/* Score game for players */
	score_game(&sim);

	/* Remember old scores */
	old_vp[0] = sim.p[defender].end_vp;
	old_vp[1] = sim.p[attacker].end_vp;

	/* Loop over table cards for the attacker */
	for (i = 0; i < table_size[attacker]; i++)
	{
		/* Get displayed card pointer */
		j_ptr = &table[attacker][i];

		/* Skip non-eligible cards */
		if (!j_ptr->eligible) continue;

		/* Get next takeover struct */
		t_ptr = takeovers + num_takeovers;
		++total_takeovers;

		/* Store card */
		t_ptr->card = j_ptr->index;

		/* Compute attack strength */
		t_ptr->attack = strength_against(g, attacker, which, j_ptr->index, 0);

		/* Copy game */
		sim = *g;

		/* Set simulated game */
		sim.simulation = 1;

		/* Assume the takeover always succeeds */
		sim.p[attacker].bonus_military = 100;

		/* Simulate usage of the takeover callback, back out if illegal */
		if (!takeover_callback(&sim, t_ptr->card, which))
			continue;

		/* Increase the takeover count */
		++num_takeovers;

		/* Check for non-military target */
		if (!(c_ptr->d_ptr->flags & FLAG_MILITARY))
		{
			/* Loop over cards in table */
			for (j = 0; j < table_size[attacker]; ++j)
			{
				/* Get card */
				d_ptr = g->deck[table[attacker][j].index].d_ptr;

				/* Loop over powers */
				for (k = 0; k < d_ptr->num_power; ++k)
				{
					/* Check for "conquer peaceful" power */
					if (d_ptr->powers[k].code & P3_CONQUER_SETTLE &&
					    !(d_ptr->powers[k].code & P3_NO_TAKEOVER))
					{
						/* XXX Discard card */
						move_card(&sim, table[attacker][j].index,
						          -1, WHERE_DISCARD);
						break;
					}
				}
			}
		}

		/* Simulate the takeover resolution */
		resolve_takeover(&sim, attacker, which, t_ptr->card, 0, 1);

		/* Simulate end of phase (for self-scoring cards) */
		clear_temp(&sim);

		/* Apply goals */
		check_goals(&sim);

		/* Score game for players */
		score_game(&sim);

		/* Compute score differences */
		t_ptr->vp_diff[0] = sim.p[defender].end_vp - old_vp[0];
		t_ptr->vp_diff[1] = sim.p[attacker].end_vp - old_vp[1];
	}

	/* Set text pointer */
	p = text;

	/* Check for old tool tip */
	if (i_ptr->tooltip)
	{
		/* Keep previous tool tip */
		strcpy(p, i_ptr->tooltip);

		/* Advance text pointer */
		p += strlen(p);

		/* Add newline */
		p += sprintf(p, "\n");

		/* Free old tool tip */
		free(i_ptr->tooltip);
	}

	/* Add defense */
	p += sprintf(p, "Current defense: %d", defense);

	/* Check for only one power */
	if (total_takeovers == 1)
	{
		/* Get takeover struct */
		t_ptr = &takeovers[0];

		/* Add attack strength */
		p += sprintf(p, "\nCurrent attack: %d", t_ptr->attack);

		/* Add defender vp diff */
		p += sprintf(p, "\n%s: %d VP%s",
		             g->p[defender].name,
		             t_ptr->vp_diff[0], PLURAL(t_ptr->vp_diff[0]));

		/* Add attacker vp diff */
		p += sprintf(p, "\n%s: %d VP%s",
		             g->p[attacker].name,
		             t_ptr->vp_diff[1], PLURAL(t_ptr->vp_diff[1]));
	}
	else
	{
		/* Loop over all takeovers powers */
		for (i = 0; i < num_takeovers; ++i)
		{
			/* Get takeover struct */
			t_ptr = &takeovers[i];

			/* Add name of card */
			p += sprintf(p, "\nUsing %s:", g->deck[t_ptr->card].d_ptr->name);

			/* Add attack strength */
			p += sprintf(p, "\n  Current attack: %d", t_ptr->attack);

			/* Add defender vp diff */
			p += sprintf(p, "\n  %s: %d VP%s",
			             g->p[defender].name,
			             t_ptr->vp_diff[0], PLURAL(t_ptr->vp_diff[0]));

			/* Add attacker vp diff */
			p += sprintf(p, "\n  %s: %d VP%s",
			             g->p[attacker].name,
			             t_ptr->vp_diff[1], PLURAL(t_ptr->vp_diff[1]));
		}
	}

	/* Return the text */
	return strdup(text);
}

/*
 * Create a tooltip for a card displayed on a table during trade.
 */
static char *card_trade_tooltip(game *g, int who, displayed *i_ptr,
                                int no_bonus)
{
	char text[1024];
	card *c_ptr;
	int type;

	/* Get card pointer */
	c_ptr = &g->deck[i_ptr->index];

	/* Get good type */
	type = c_ptr->d_ptr->good_type;

	/* Check for "any" kind */
	if (type == GOOD_ANY)
	{
		/* Format tool tip for all kinds */
		sprintf(text, "Trade value:\n  Novelty: %d\n  Rare: %d\n"
		        "  Genes: %d\n  Alien: %d",
		        trade_value(g, who, c_ptr, GOOD_NOVELTY, no_bonus),
		        trade_value(g, who, c_ptr, GOOD_RARE, no_bonus),
		        trade_value(g, who, c_ptr, GOOD_GENE, no_bonus),
		        trade_value(g, who, c_ptr, GOOD_ALIEN, no_bonus));
	}
	else
	{
		/* Check for multiple goods */
		if (c_ptr->num_goods >= 2)
		{
			/* Format tool tip */
			sprintf(text, "%d goods\nTrade value: %d",
			        c_ptr->num_goods,
			        trade_value(g, who, c_ptr, type, no_bonus));
		}
		else
		{
			/* Format tool tip */
			sprintf(text, "Trade value: %d",
			        trade_value(g, who, c_ptr, type, no_bonus));
		}
	}

	/* Return text */
	return strdup(text);
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

			/* Error */
			default:
				image = NULL;
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

	/* Add VP tooltip */
	gtk_widget_set_tooltip_markup(image, s_ptr->vp_tip);

	/* Pack icon into status box */
	gtk_box_pack_start(GTK_BOX(box), image, FALSE, FALSE, 0);

	/* Check for third expansion */
	if (real_game.expanded == 3)
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

		/* Add Prestige tooltip */
		gtk_widget_set_tooltip_markup(image, s_ptr->prestige_tip);

		/* Pack icon into status box */
		gtk_box_pack_start(GTK_BOX(box), image, FALSE, FALSE, 0);
	}

	/* Check for discount enabled and discount tip */
	if (opt.settle_discount && strlen(s_ptr->discount_tip))
	{
		/* Create discount icon image */
		buf = gdk_pixbuf_scale_simple(icon_cache[ICON_DISCOUNT], height, height,
		                              GDK_INTERP_BILINEAR);

		/* Create image from pixbuf */
		image = gtk_image_new_from_pixbuf(buf);

		/* Pointer to extra info structure */
		ei = &status_extra_info[who][3];

		/* Create text for general discount */
		sprintf(ei->text, "<b>-%d</b>", s_ptr->discount.base);

		/* Set font */
		ei->fontstr = "Sans 10";

		/* No border */
		ei->border = 0;

		/* Connect expose-event to draw extra text */
		g_signal_connect_after(G_OBJECT(image), "expose-event",
							   G_CALLBACK(draw_extra_text), ei);

		/* Destroy our copy of the icon */
		g_object_unref(G_OBJECT(buf));

		/* Add discount tooltip */
		gtk_widget_set_tooltip_text(image, s_ptr->discount_tip);

		/* Pack icon into status box */
		gtk_box_pack_start(GTK_BOX(box), image, FALSE, FALSE, 0);
	}

	if (strlen(s_ptr->military_tip))
	{
		/* Create military icon image */
		buf = gdk_pixbuf_scale_simple(icon_cache[ICON_MILITARY], height, height,
		                              GDK_INTERP_BILINEAR);

		/* Create image from pixbuf */
		image = gtk_image_new_from_pixbuf(buf);

		/* Pointer to extra info structure */
		ei = &status_extra_info[who][4];

		/* Create text for military strength */
		sprintf(ei->text, "<span foreground=\"red\" weight=\"bold\">%+d</span>",
		        s_ptr->military.base);

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
	}

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
 * Overlays the overlay pixbuf on the background pixbuf,
 * using the top current/max portion of the image only.
 */
GdkPixbuf *overlay(GdkPixbuf *background, GdkPixbuf *overlay,
                   double size, double current, double max)
{
	GdkPixbuf *buf;
	double ratio, height_scale, width_scale;

	/* Compute fill ratio */
	ratio = current / max;

	/* Check if ratio is negative */
	if (ratio <= 0)
	{
		/* Use the scaled overlay */
		buf = gdk_pixbuf_scale_simple(overlay, (int) size, (int) size,
		                              GDK_INTERP_BILINEAR);
	}
	else
	{
		/* Scale the background */
		buf = gdk_pixbuf_scale_simple(background, (int) size, (int) size,
		                              GDK_INTERP_BILINEAR);

		/* Check if ratio is less than full */
		if (ratio < 1)
		{
			/* Compute scale ratios */
			height_scale = size / gdk_pixbuf_get_height(overlay);
			width_scale = size / gdk_pixbuf_get_width(overlay);

			/* Overlay the alternative vp image */
			gdk_pixbuf_composite(overlay, buf,
			                     0, 0,
			                     (int) size, (int) (size * (1 - ratio)),
			                     0, 0,
			                     height_scale, width_scale,
			                     GDK_INTERP_BILINEAR, 255);
		}
	}

	/* Return the pixbuf */
	return buf;
}

/*
 * Redraw game's and all player's status information.
 */
void redraw_status(void)
{
	GtkWidget *draw_image, *discard_image, *pool_image;
	GdkPixbuf *buf;
	int i;
	struct extra_info *ei;
	const int size = 48;

	/* Loop over players */
	for (i = 0; i < real_game.num_players; i++)
	{
		/* Redraw player's status */
		redraw_status_area(i, player_status[i]);
	}

	/* First destroy all pre-existing status widgets */
	gtk_container_foreach(GTK_CONTAINER(game_status), destroy_widget, NULL);

	/* Build deck image */
	buf = overlay(icon_cache[ICON_DRAW], icon_cache[ICON_DRAW_EMPTY], size,
	              display_deck, real_game.deck_size);

#if 0
	/* Add note if game is tampered */
	if (game_tampered)
	{
		/* Check for card moved in debug */
		if (game_tampered & TAMPERED_MOVE) color = 0x00ff0000;

		/* Check for looked at debug dialog */
		else if (game_tampered & TAMPERED_LOOK) color = 0x00ffcc00;

		/* Check for undo used */
		else if (game_tampered & TAMPERED_UNDO) color = 0x00dddd00;

		/* Check for seed used */
		else if (game_tampered & TAMPERED_SEED) color = 0x00666666;

		/* Check for loaded game */
		else if (game_tampered & TAMPERED_LOAD) color = 0x00ffffff;

		/* Check for saved game */
		else if (game_tampered & TAMPERED_SAVE) color = 0x00ffffff;

		/* Add a tiny square at the bottom left */
		gdk_pixbuf_composite_color(buf, buf,
		                           0, size - 3, 3, 3,
		                           0, 0, 1, 1,
		                           GDK_INTERP_BILINEAR, 255,
		                           0, 0, 16,
		                           color, 0);
	}
#endif

	/* Make image widget */
	draw_image = gtk_image_new_from_pixbuf(buf);

	/* Pointer to extra info structure */
	ei = &game_extra_info[0];

	/* Create text for deck */
	sprintf(ei->text, "<b>%d\n<span font=\"10\">Deck</span></b>",
	        display_deck);

	/* Set font */
	ei->fontstr = "Sans 12";

	/* Draw text a border */
	ei->border = 1;

	/* Connect expose-event to draw extra text */
	g_signal_connect_after(G_OBJECT(draw_image), "expose-event",
	                       G_CALLBACK(draw_extra_text), ei);

	/* Destroy our copy of the icon */
	g_object_unref(G_OBJECT(buf));

	/* Pack deck image */
	gtk_box_pack_start(GTK_BOX(game_status), draw_image, TRUE, TRUE, 0);

	/* Build discard image */
	buf = gdk_pixbuf_scale_simple(icon_cache[ICON_HANDSIZE], size, (int) size,
	                              GDK_INTERP_BILINEAR);

	/* Make image widget */
	discard_image = gtk_image_new_from_pixbuf(buf);

	/* Pointer to extra info structure */
	ei = &game_extra_info[1];

	/* Create text for discard */
	sprintf(ei->text, "<b>%d\n<span font=\"10\">Discard</span></b>",
	        display_discard);

	/* Set font */
	ei->fontstr = "Sans 12";

	/* Draw text a border */
	ei->border = 1;

	/* Connect expose-event to draw extra text */
	g_signal_connect_after(G_OBJECT(discard_image), "expose-event",
	                       G_CALLBACK(draw_extra_text), ei);

	/* Destroy our copy of the icon */
	g_object_unref(G_OBJECT(buf));

	/* Pack discard image */
	gtk_box_pack_start(GTK_BOX(game_status), discard_image, TRUE, TRUE, 0);

	/* Build VP image */
	buf = overlay(icon_cache[ICON_VP], icon_cache[ICON_VP_EMPTY],
	              size, display_pool, real_game.num_players * 12 +
	                (real_game.expanded == 3 ? 5 : 0));

	/* Make VP widget */
	pool_image = gtk_image_new_from_pixbuf(buf);

	/* Pointer to extra info structure */
	ei = &game_extra_info[2];

	/* Create text for VPs */
	sprintf(ei->text, "<b>%d\n<span font=\"10\">Pool</span></b>",
	        display_pool);

	/* Set font */
	ei->fontstr = "Sans 12";

	/* Draw text a border */
	ei->border = 1;

	/* Connect expose-event to draw extra text */
	g_signal_connect_after(G_OBJECT(pool_image), "expose-event",
	                       G_CALLBACK(draw_extra_text), ei);

	/* Destroy our copy of the icon */
	g_object_unref(G_OBJECT(buf));

	/* Pack VP image */
	gtk_box_pack_start(GTK_BOX(game_status), pool_image, TRUE, TRUE, 0);

	/* Show everything */
	gtk_widget_show_all(game_status);
}

/*
 * Map from actions to phase icons.
 */
static int phase_icon[] = { 0, 23, 23, 3, 3, 5, 5, 24, 24, 9 };

/*
 * Draw phase indicators.
 */
void redraw_phase(void)
{
	int i, size = 40;
	GdkPixbuf *buf, *border_buf, *blank_buf;
	GtkWidget *image;

	/* Recompute size */
	if (real_game.advanced && opt.log_width && opt.log_width < 7 * size)
		size = opt.log_width / 7;
	else if (!real_game.advanced && opt.log_width &&
		 opt.log_width < 5 * size)
		size = opt.log_width / 5;

	/* First destroy all pre-existing widgets */
	gtk_container_foreach(GTK_CONTAINER(phase_box), destroy_widget, NULL);

	/* Loop over actions */
	for (i = ACT_EXPLORE_5_0; i <= ACT_PRODUCE; i++)
	{
		/* Skip second explore/consume actions */
		if (i == ACT_EXPLORE_1_1 || i == ACT_CONSUME_X2) continue;

		/* Check for basic game and advanced actions */
		if (!real_game.advanced &&
		    (i == ACT_DEVELOP2 || i == ACT_SETTLE2))
		{
			/* Skip action */
			continue;
		}

		/* Scale phase icon down to correct size */
		buf = gdk_pixbuf_scale_simple(icon_cache[phase_icon[i]],
		                              size, size, GDK_INTERP_BILINEAR);

		/* Check for inactive phase */
		if (!real_game.action_selected[i])
		{
			/* Desaturate */
			gdk_pixbuf_saturate_and_pixelate(buf, buf, 0, TRUE);
		}

		/* Check for current phase */
		else if (real_game.cur_action == i)
		{
			/* Create a border pixbuf */
			border_buf = gdk_pixbuf_new(GDK_COLORSPACE_RGB, TRUE, 8,
			                            size, size);

			/* Fill pixbuf with highlight color */
			gdk_pixbuf_fill(border_buf, 0x0000ff99);

			/* Create a blank pixbuf */
			blank_buf = gdk_pixbuf_new(GDK_COLORSPACE_RGB, TRUE, 8,
			                           size, size);

			/* Fill pixbuf with transparent black */
			gdk_pixbuf_fill(blank_buf, 0);

			/* Copy blank space onto middle of border buffer */
			gdk_pixbuf_copy_area(blank_buf, 3, 3,
			                     size - 6, size - 6,
			                     border_buf, 3, 3);

			/* Composite border onto phase image buffer */
			gdk_pixbuf_composite(border_buf, buf, 0, 0, size, size, 0, 0, 1, 1,
			                     GDK_INTERP_BILINEAR, 255);

			/* Release our copies of the pixbufs */
			g_object_unref(G_OBJECT(blank_buf));
			g_object_unref(G_OBJECT(border_buf));
		}

		/* Make image widget */
		image = gtk_image_new_from_pixbuf(buf);

		/* Destroy our copy of the icon */
		g_object_unref(G_OBJECT(buf));

		/* Add tooltip */
		gtk_widget_set_tooltip_markup(image, plain_actname[i]);

		/* Pack icon into box */
		gtk_box_pack_start(GTK_BOX(phase_box), image, FALSE, FALSE, 0);
	}

	/* Show everything */
	gtk_widget_show_all(phase_box);
}

/*
 * Redraw everything.
 */
void redraw_everything(void)
{
	/* Redraw hand, table, and status area */
	redraw_status();
	redraw_table();
	redraw_hand();
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
	req_height = (widget->allocation.width / 8) * CARD_HEIGHT / CARD_WIDTH;

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
	redraw_table();
	redraw_hand();
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
 * Compute settle discounts for a player.
 */
static void compute_discounts(game *g, int who, discounts *d_ptr)
{
	power_where w_list[100];
	power *o_ptr;
	int i, n;

	/* Clear discounts */
	memset(d_ptr, 0, sizeof(discounts));

	/* Set bonus discounts */
	d_ptr->bonus = g->p[who].bonus_reduce;

	/* Check for prestige settle */
	if ((g->cur_action == ACT_SETTLE || g->cur_action == ACT_SETTLE2) &&
	    player_chose(g, who, ACT_PRESTIGE | g->cur_action))
	{
		/* Add prestige bonus */
		d_ptr->bonus += 3;
	}

	/* Get settle phase powers */
	n = get_powers(g, who, PHASE_SETTLE, w_list);

	/* Loop over powers */
	for (i = 0; i < n; i++)
	{
		/* Get power pointer */
		o_ptr = w_list[i].o_ptr;

		/* Check discard for 0 */
		if (o_ptr->code == (P3_DISCARD | P3_REDUCE_ZERO))
			d_ptr->zero += 1;

		/* Check for reduce power */
		if (o_ptr->code & P3_REDUCE)
		{
			/* Check for general discount */
			if (o_ptr->code == P3_REDUCE)
				d_ptr->base += o_ptr->value;

			/* Check for discount against Novelty worlds */
			if (o_ptr->code & P3_NOVELTY)
				d_ptr->specific[GOOD_NOVELTY] += o_ptr->value;

			/* Check for discount against Rare worlds */
			if (o_ptr->code & P3_RARE)
				d_ptr->specific[GOOD_RARE] += o_ptr->value;

			/* Check for discount against Genes worlds */
			if (o_ptr->code & P3_GENE)
				d_ptr->specific[GOOD_GENE] += o_ptr->value;

			/* Check for discount against Alien worlds */
			if (o_ptr->code & P3_ALIEN)
				d_ptr->specific[GOOD_ALIEN] += o_ptr->value;
		}

		/* Check for pay-for-military powers */
		if (o_ptr->code & P3_PAY_MILITARY)
		{
			/* Check for non-alien power without discount */
			if (o_ptr->code == P3_PAY_MILITARY && o_ptr->value == 0)
				d_ptr->non_alien_mil_0 = TRUE;

			/* Check for non-alien power with discount */
			if (o_ptr->code == P3_PAY_MILITARY && o_ptr->value == 1)
				d_ptr->non_alien_mil_1 = TRUE;

			/* Check for rebel flag */
			if (o_ptr->code & P3_AGAINST_REBEL)
				d_ptr->rebel_mil_2 = TRUE;

			/* Check for chromo flag */
			if (o_ptr->code & P3_AGAINST_CHROMO)
				d_ptr->chromo_mil = TRUE;

			/* Check for alien flag */
			if (o_ptr->code & P3_ALIEN)
				d_ptr->alien_mil = TRUE;
		}

		/* Check for pay-for-military discount */
		if (o_ptr->code & P3_PAY_DISCOUNT)
			d_ptr->pay_discount += o_ptr->value;

		/* Check for conquer settle without discount */
		if ((o_ptr->code & P3_CONQUER_SETTLE) && o_ptr->value == 0)
			d_ptr->conquer_settle_0 = TRUE;

		/* Check for conquer settle with discount */
		if ((o_ptr->code & P3_CONQUER_SETTLE) && o_ptr->value == 2)
			d_ptr->conquer_settle_2 = TRUE;
	}

	/* Check for any modifiers */
	d_ptr->has_data = d_ptr->base || d_ptr->bonus ||
		d_ptr->specific[GOOD_NOVELTY] || d_ptr->specific[GOOD_RARE] ||
		d_ptr->specific[GOOD_GENE] || d_ptr->specific[GOOD_ALIEN] ||
		d_ptr->zero || d_ptr->pay_discount ||
		d_ptr->non_alien_mil_0 || d_ptr->non_alien_mil_1 ||
		d_ptr->rebel_mil_2 || d_ptr->chromo_mil || d_ptr->alien_mil ||
		d_ptr->conquer_settle_0 || d_ptr->conquer_settle_2;
}

/*
 * Compute military strength for a player.
 */
static void compute_military(game *g, int who, mil_strength *m_ptr)
{
	card *c_ptr;
	power *o_ptr;
	int x, i, hand_size, hand_military = 0, rare_goods;

	/* Start strengths at 0 */
	memset(m_ptr, 0, sizeof(mil_strength));

	/* Begin with base military strength */
	m_ptr->base = total_military(g, who);

	/* Set bonus military */
	m_ptr->bonus = g->p[who].bonus_military;

	/* Get first active card */
	x = g->p[who].start_head[WHERE_ACTIVE];

	/* Count number of rare goods */
	rare_goods = get_goods(g, who, NULL, GOOD_RARE);

	/* Loop over cards */
	for ( ; x != -1; x = g->deck[x].start_next)
	{
		/* Get card pointer */
		c_ptr = &g->deck[x];

		/* Loop over card's powers */
		for (i = 0; i < c_ptr->d_ptr->num_power; i++)
		{
			/* Get power pointer */
			o_ptr = &c_ptr->d_ptr->powers[i];

			/* Skip incorrect phase */
			if (o_ptr->phase != PHASE_SETTLE) continue;

			/* Check for discard power */
			if ((o_ptr->code & P3_DISCARD) && c_ptr->where == WHERE_DISCARD)
				continue;

			/* Check for defense power */
			if (o_ptr->code & P3_TAKEOVER_DEFENSE && takeovers_enabled(g))
			{
				/* Add defense for military worlds */
				m_ptr->defense +=
					count_active_flags(g, who, FLAG_MILITARY);

				/* Add extra defense for Rebel military worlds */
				m_ptr->defense +=
					count_active_flags(g, who, FLAG_REBEL | FLAG_MILITARY);
			}

			/* Check for takeover imperium power */
			if (o_ptr->code & P3_TAKEOVER_IMPERIUM && takeovers_enabled(g))
			{
				/* Set imperium attack */
				m_ptr->attack_imperium =
					2 * count_active_flags(g, who, FLAG_REBEL | FLAG_MILITARY);

				/* Check if card name already set */
				if (strlen(m_ptr->imp_card))
				{
					/* XXX Use name of both cards */
					strcpy(m_ptr->imp_card, "Rebel Alliance/Rebel Sneak Attack");
				}
				else
				{
					/* Remember name of card */
					strcpy(m_ptr->imp_card, c_ptr->d_ptr->name);
				}
			}

			/* Skip used powers */
			if (c_ptr->misc & (1 << (MISC_USED_SHIFT + i))) continue;

			/* Check for military from hand */
			if (o_ptr->code & P3_MILITARY_HAND)
				hand_military += o_ptr->value;

			/* Skip non-military powers */
			if (!(o_ptr->code & P3_EXTRA_MILITARY)) continue;

			/* Check for discard for military */
			if (o_ptr->code & P3_DISCARD)
				m_ptr->max_bonus += o_ptr->value;

			/* Check for prestige for military */
			if ((o_ptr->code & P3_CONSUME_PRESTIGE) && g->p[who].prestige)
				m_ptr->max_bonus += o_ptr->value;

			/* Check for good for military */
			if ((o_ptr->code & P3_CONSUME_RARE) && rare_goods)
			{
				m_ptr->max_bonus += o_ptr->value;
				--rare_goods;
			}

			/* Check for strength against rebels */
			if (o_ptr->code & P3_AGAINST_REBEL)
				m_ptr->rebel += o_ptr->value;

			/* Check for strength against Novelty worlds */
			if (o_ptr->code & P3_NOVELTY)
				m_ptr->specific[GOOD_NOVELTY] += o_ptr->value;

			/* Check for strength against Rare worlds */
			if (o_ptr->code & P3_RARE)
				m_ptr->specific[GOOD_RARE] += o_ptr->value;

			/* Check for strength against Genes worlds */
			if (o_ptr->code & P3_GENE)
				m_ptr->specific[GOOD_GENE] += o_ptr->value;

			/* Check for strength against Alien worlds */
			if (o_ptr->code & P3_ALIEN)
				m_ptr->specific[GOOD_ALIEN] += o_ptr->value;
		}
	}

	/* Get player hand size */
	hand_size = count_player_area(g, who, WHERE_HAND);

	/* Reduce maximum military from hand */
	if (hand_size < hand_military) hand_military = hand_size;

	/* Add military from hand to max temporary military */
	m_ptr->max_bonus += hand_military;

	/* Check for takeovers enabled and imperium card played */
	m_ptr->imperium = takeovers_enabled(g) &&
		count_active_flags(g, who, FLAG_IMPERIUM);

	/* Check for takeovers enabled and rebel military world played */
	m_ptr->military_rebel = takeovers_enabled(g) &&
		count_active_flags(g, who, FLAG_MILITARY | FLAG_REBEL);

	/* Check for any modifiers */
	m_ptr->has_data = m_ptr->base || m_ptr->bonus || m_ptr->rebel ||
		m_ptr->specific[GOOD_NOVELTY] || m_ptr->specific[GOOD_RARE] ||
		m_ptr->specific[GOOD_GENE] || m_ptr->specific[GOOD_ALIEN] ||
		m_ptr->defense || m_ptr->attack_imperium || m_ptr->imperium ||
		m_ptr->military_rebel || m_ptr->max_bonus;
}

/*
 * Reset a displayed card structure.
 */
static void reset_display(displayed *i_ptr)
{
	/* Check for tooltip to be freed */
	if (i_ptr->tooltip) free(i_ptr->tooltip);

	/* Clear all fields */
	memset(i_ptr, 0, sizeof(displayed));
}

/*
 * Function to compare two cards on the table for sorting.
 */
static int cmp_table(const void *t1, const void *t2)
{
	displayed *i_ptr1 = (displayed *)t1, *i_ptr2 = (displayed *)t2;

	/* Sort by order played */
	if (i_ptr1->order != i_ptr2->order)
		return i_ptr1->order - i_ptr2->order;

	/* Then sort by cardi index */
	return i_ptr1->index - i_ptr2->index;
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

		/* Reset structure */
		reset_display(i_ptr);

		/* Add card information */
		i_ptr->index = i;
		i_ptr->d_ptr = c_ptr->d_ptr;

		/* Card is in hand */
		i_ptr->hand = 1;

		/* Set color flag */
		i_ptr->color = color;

		/* Check for vp in hand enabled */
		if (opt.vp_in_hand)
		{
			/* Get tool tip */
			i_ptr->tooltip = card_hand_tooltip(g, player_us, i);
		}
	}
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

		/* Reset structure */
		reset_display(i_ptr);

		/* Add card information */
		i_ptr->index = i;
		i_ptr->d_ptr = c_ptr->d_ptr;

		/* Set color flag */
		i_ptr->color = color;

		/* Check for good */
		i_ptr->num_goods = c_ptr->num_goods;

		/* Copy order played */
		i_ptr->order = c_ptr->order;

		/* Get tooltip */
		i_ptr->tooltip = card_table_tooltip(g, player_us, i);
	}

	/* Sort list */
	qsort(table[who], table_size[who], sizeof(displayed), cmp_table);
}

/*
 * Reset status information for a player.
 */
void reset_status(game *g, int who)
{
	int i;

	/* Check for needing to add [AI] to player name */
	if (g->p[who].ai && strstr(g->p[who].name, "[AI]") != g->p[who].name)
	{
		/* Add [AI] to player name */
		sprintf(status_player[who].name, "[AI] %s", g->p[who].name);
	}
	else
	{
		/* Copy player's name */
		strcpy(status_player[who].name, g->p[who].name);
	}

	/* Check for actions known */
	if (g->advanced && g->cur_action < ACT_SEARCH && who == player_us &&
	    count_active_flags(g, player_us, FLAG_SELECT_LAST))
	{
		/* Copy first action only */
		status_player[who].action[0] = g->p[who].action[0];
		status_player[who].action[1] = ICON_NO_ACT;
	}
	else if (g->cur_action >= ACT_SEARCH ||
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

	/* Count general discount */
	compute_discounts(g, who, &status_player[who].discount);

	/* Count military strength */
	compute_military(g, who, &status_player[who].military);

	/* Get text of vp tooltip */
	strcpy(status_player[who].vp_tip, get_vp_tooltip(g, who));

	/* Get text of discount tooltip */
	strcpy(status_player[who].discount_tip,
	       get_discount_tooltip(&status_player[who].discount));

	/* Get text of military tooltip */
	strcpy(status_player[who].military_tip,
	       get_military_tooltip(&status_player[who].military));

	/* Get text of prestige tooltip */
	strcpy(status_player[who].prestige_tip, get_prestige_tooltip(g, who));

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
static GtkWidget *action_toggle[MAX_ACTION], *prestige_toggle;


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

		/* Skip unavailable and unselected */
		if (!toggle ||
		    !gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(toggle)))
		{
			/* Skip action */
			continue;
		}

		/* Skip unselectable */
		if (!GTK_WIDGET_SENSITIVE(toggle)) continue;

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
 * Callback when action choice changes in advanced game.
 */
static void action_choice_changed_advanced(GtkToggleButton *button,
                                           gpointer data)
{
	int i = GPOINTER_TO_INT(data);

	/* Check for toggled button */
	if (gtk_toggle_button_get_active(button))
	{
		/* Increment count of buttons pressed */
		actions_chosen++;
	}
	else
	{
		/* Decrement count of buttons pressed */
		actions_chosen--;
	}

	/* Check for needing to reset prestige action */
	if (i == prestige_action || (i == ACT_SEARCH && prestige_action != -1))
	{
		/* Reset icon to non-prestige version */
		reset_action_icon(GTK_WIDGET(action_toggle[prestige_action]),
		                  prestige_action);

		/* Clear prestige boost */
		prestige_action = -1;
	}

	/* Check for search action toggle while prestige toggle visible */
	if (i == ACT_SEARCH && prestige_toggle)
	{
		/* Change the sensitivity of the prestige toggle button */
		gtk_widget_set_sensitive(GTK_WIDGET(prestige_toggle),
		                         !gtk_toggle_button_get_active(button));
	}

	/* Check for exactly 2 actions chosen */
	gtk_widget_set_sensitive(action_button, actions_chosen == 2);
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
		/* Change prestige action, if any */
		if (prestige_action != -1)
		{
			/* Get current prestige action */
			j = prestige_action;

			/* Reset icon */
			reset_action_icon(action_toggle[j], j);

			/* Check for pressed search button */
			if (i == ACT_SEARCH)
			{
				/* Clear prestige action */
				prestige_action = -1;
			}
			else
			{
				/* Change prestige action */
				prestige_action = i;

				/* Reset icon */
				reset_action_icon(GTK_WIDGET(button), i | ACT_PRESTIGE);
			}
		}
	}
	else
	{
		/* Reset icon */
		reset_action_icon(GTK_WIDGET(button), i);
	}
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
 * Callback for opening a combo's selection box.
 */
static void combo_open(GtkWidget *widget, gpointer data)
{
	/* Popup the combo box */
	gtk_combo_box_popup(GTK_COMBO_BOX(widget));
}

/*
 * Callback for moving a combo's selection up one item.
 */
static void combo_up(GtkWidget *widget, gpointer data)
{
	int i = gtk_combo_box_get_active(GTK_COMBO_BOX(widget));

	/* Move selection up if we are not already at the top */
	if (i > 0) gtk_combo_box_set_active(GTK_COMBO_BOX(widget), i - 1);
}

/*
 * Callback for moving a combo's selection down one item.
 */
static void combo_down(GtkWidget *widget, gpointer data)
{
	int i = gtk_combo_box_get_active(GTK_COMBO_BOX(widget));

	/* Move selection down if we are not already at the bottom */
	if (i + 1 < GPOINTER_TO_INT(data))
		gtk_combo_box_set_active(GTK_COMBO_BOX(widget), i + 1);
}

/*
 * Choose two actions.
 */
static void gui_choose_action_advanced(game *g, int who, int action[2], int one)
{
	GtkWidget *image, *label;
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
		if (i == ACT_SEARCH && (g->expanded != 3 ||
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
		                           window_accel,
		                           accel_keys[act_to_accel[i]],
		                           accel_mods[act_to_accel[i]], 0);

		/* Check if client is disconnected */
		if (client_state == CS_DISCONN)
		{
			/* XXX Wrap to '0' instead of ':' */
			if (key == GDK_1 + 9) key = GDK_0;

			/* Add hander for numeric keypresses */
			gtk_widget_add_accelerator(action_toggle[i], "key-signal",
			                           window_accel, key++, 0, 0);
		}

		/* Connect "toggled" signal */
		g_signal_connect(G_OBJECT(action_toggle[i]), "toggled",
		                 G_CALLBACK(action_choice_changed_advanced),
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
	if (real_game.expanded == 3 && !real_game.p[who].prestige_action_used &&
	    real_game.p[who].prestige > 0 && prestige_action == -1)
	{
		/* Create button to toggle prestige */
		prestige_toggle = gtk_button_new();

		/* Get icon for action */
		image = action_icon(ICON_PRESTIGE, h);

		/* Do not request height */
		gtk_widget_set_size_request(image, -1, 0);

		/* Pack image into button */
		gtk_container_add(GTK_CONTAINER(prestige_toggle), image);

		/* Pack button into action box */
		gtk_box_pack_start(GTK_BOX(action_box), prestige_toggle, FALSE,
		                   FALSE, h);

		/* Create tooltip for button */
		gtk_widget_set_tooltip_text(prestige_toggle, "Prestige");

		/* Add handler for keypresses */
		gtk_widget_add_accelerator(prestige_toggle, "key-signal",
		                           window_accel,
		                           accel_keys[6],
		                           accel_mods[6], 0);

		/* Check if client is disconnected */
		if (client_state == CS_DISCONN)
		{
			/* Add 'P' keypress */
			gtk_widget_add_accelerator(prestige_toggle, "key-signal",
			                           window_accel, GDK_p, 0, 0);
		}

		/* Connect "pointer enter" signal */
		g_signal_connect(G_OBJECT(prestige_toggle), "enter-notify-event",
		                 G_CALLBACK(redraw_action),
		                 GINT_TO_POINTER(10));

		/* Connect "pressed" signal */
		g_signal_connect(G_OBJECT(prestige_toggle), "pressed",
		                 G_CALLBACK(prestige_pressed), NULL);

		/* Connect key-signal */
		g_signal_connect(G_OBJECT(prestige_toggle), "key-signal",
		                 G_CALLBACK(prestige_pressed), NULL);

		/* Show everything */
		gtk_widget_show_all(prestige_toggle);
	}
	else
	{
		/* Reset the prestige toggle pointer */
		prestige_toggle = NULL;
	}

	/* Create filler label */
	label = gtk_label_new("");

	/* Add label after action buttons */
	gtk_box_pack_start(GTK_BOX(action_box), label, TRUE, TRUE, 0);

	/* Show label */
	gtk_widget_show(label);

	/* Process events */
	gtk_main();

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
	if (prestige_toggle) gtk_widget_destroy(prestige_toggle);

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
	GtkWidget *prestige = NULL, *image, *label, *group;
	int i, h, key = GDK_1;

	/* Check for advanced game */
	if (real_game.advanced)
	{
		/* Call advanced function instead */
		return gui_choose_action_advanced(g, who, action, one);
	}

	/* Do not boost any action yet */
	prestige_action = -1;

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
		action_toggle[i] = NULL;

		/* Check for unusable search action */
		if (i == ACT_SEARCH && (real_game.expanded != 3 ||
		                        real_game.p[who].prestige_action_used))
		{
			/* Skip search action */
			continue;
		}

		/* Skip second develop/settle */
		if (i == ACT_DEVELOP2 || i == ACT_SETTLE2)
		{
			/* Clear button */
			action_toggle[i] = NULL;
			continue;
		}

		/* Create radio button */
		action_toggle[i] = gtk_radio_button_new_from_widget(
		                                        GTK_RADIO_BUTTON(group));

		/* Remember grouping */
		group = action_toggle[i];

		/* Get icon for action */
		image = action_icon(i, h);

		/* Do not request height */
		gtk_widget_set_size_request(image, -1, 0);

		/* Draw button without separate indicator */
		gtk_toggle_button_set_mode(GTK_TOGGLE_BUTTON(action_toggle[i]), FALSE);

		/* Pack image into button */
		gtk_container_add(GTK_CONTAINER(action_toggle[i]), image);

		/* Pack button into action box */
		gtk_box_pack_start(GTK_BOX(action_box), action_toggle[i], FALSE,
		                   FALSE, 0);

		/* Create tooltip for button */
		gtk_widget_set_tooltip_text(action_toggle[i], action_name(i));

		/* Add handler for keypresses */
		gtk_widget_add_accelerator(action_toggle[i], "key-signal",
		                           window_accel, accel_keys[act_to_accel[i]],
		                           accel_mods[act_to_accel[i]], 0);

		/* Check if client is disconnected */
		if (client_state == CS_DISCONN)
		{
			/* Add hander for numeric keypresses */
			gtk_widget_add_accelerator(action_toggle[i], "key-signal",
			                           window_accel, key++, 0, 0);
		}

		/* Connect "pointer enter" signal */
		g_signal_connect(G_OBJECT(action_toggle[i]), "enter-notify-event",
		                 G_CALLBACK(redraw_action), GINT_TO_POINTER(i));

		/* Connect "toggled" signal */
		g_signal_connect(G_OBJECT(action_toggle[i]), "toggled",
		                 G_CALLBACK(action_choice_changed),
		                 GINT_TO_POINTER(i));

		/* Connect key-signal */
		g_signal_connect(G_OBJECT(action_toggle[i]), "key-signal",
		                 G_CALLBACK(action_keyed), NULL);

		/* Show everything */
		gtk_widget_show_all(action_toggle[i]);
	}

	/* Check for usable prestige action */
	if (real_game.expanded == 3 && !real_game.p[who].prestige_action_used &&
	    real_game.p[who].prestige > 0)
	{
		/* Create toggle button for prestige */
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

		/* Create tooltip for button */
		gtk_widget_set_tooltip_text(prestige, "Prestige");

		/* Add handler for keypresses */
		gtk_widget_add_accelerator(prestige, "key-signal",
		                           window_accel,
		                           accel_keys[6],
		                           accel_mods[6], 0);

		/* Check if client is disconnected */
		if (client_state == CS_DISCONN)
		{
			/* Add 'P' keypress */
			gtk_widget_add_accelerator(prestige, "key-signal",
			                           window_accel, GDK_p, 0, 0);
		}

		/* Connect "pointer enter" signal */
		g_signal_connect(G_OBJECT(prestige), "enter-notify-event",
		                 G_CALLBACK(redraw_action),
		                 GINT_TO_POINTER(10));

		/* Connect "pressed" signal */
		g_signal_connect(G_OBJECT(prestige), "pressed",
		                 G_CALLBACK(prestige_pressed), NULL);

		/* Connect key-signal */
		g_signal_connect(G_OBJECT(prestige), "key-signal",
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

	/* Loop over choices */
	for (i = 0; i < MAX_ACTION; i++)
	{
		/* Skip uncreated buttons */
		if (action_toggle[i] == NULL) continue;

		/* Check for active */
		if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(action_toggle[i])))
		{
			/* Set choice */
			action[0] = i;
			action[1] = -1;
		}
	}

	/* Check for prestige button available */
	if (prestige)
	{
		/* Check for prestige action */
		if (prestige_action != -1)
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
		if (action_toggle[i] == NULL) continue;

		/* Destroy button */
		gtk_widget_destroy(action_toggle[i]);
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

	/* Save special cards */
	num_special_cards = *num_special;
	for (i = 0; i < *num_special; ++i) special_cards[i] = &g->deck[special[i]];

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

		/* Clear displayed card */
		reset_display(i_ptr);

		/* Add card information */
		i_ptr->index = special[i];
		i_ptr->d_ptr = c_ptr->d_ptr;

		/* Card is eligible */
		i_ptr->eligible = 1;
		i_ptr->greedy = 1;

		/* Card should be highlighted when selected */
		i_ptr->highlight = HIGH_YELLOW;
		i_ptr->highlight_else = HIGH_RED;
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

				/* Card should be red when selected */
				i_ptr->highlight = HIGH_RED;

				/* Push card when selected */
				i_ptr->push = 1;
			}
		}
	}

	/* Redraw everything */
	redraw_everything();

	/* Process events */
	gtk_main();

	/* Clear special cards */
	num_special_cards = 0;

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
	card *c_ptr;
	int i, j, n = 0;
	int forced = opt.auto_select && discard == *num;

	/* Create prompt */
	sprintf(buf, "Choose %d card%s to discard", discard, PLURAL(discard));

	/* Set prompt */
	gtk_label_set_text(GTK_LABEL(action_prompt), buf);

	/* Set restrictions on action button */
	action_restrict = RESTRICT_NUM;
	action_min = action_max = discard;

	/* (De)activate action button */
	gtk_widget_set_sensitive(action_button, forced);

	/* Reset displayed cards */
	reset_cards(g, FALSE, TRUE);

	/* Loop over cards in list */
	for (i = 0; i < *num; i++)
	{
		/* Get card pointer */
		c_ptr = &g->deck[list[i]];

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

				/* Card should be red when selected */
				i_ptr->highlight = HIGH_RED;

				/* Push card when selected */
				i_ptr->push = 1;

				/* Check for new card */
				if (c_ptr->start_where != WHERE_HAND ||
				    c_ptr->start_owner != who)
				{
					/* Put gap before card */
					i_ptr->gapped = 1;
				}

				/* Check for forced choice */
				if (forced) i_ptr->selected = TRUE;
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
 * Ask the player to save one of the given cards under a world.
 */
void gui_choose_save(game *g, int who, int list[], int *num)
{
	char buf[1024];
	displayed *i_ptr;
	card *c_ptr;
	int i, j, n = 0;

	/* Save special cards */
	num_special_cards = *num;
	for (i = 0; i < *num; ++i) special_cards[i] = &g->deck[list[i]];

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
				i_ptr->greedy = 1;

				/* Display card with gap */
				i_ptr->gapped = 1;

				/* Card should be highlighted when selected */
				i_ptr->highlight = HIGH_YELLOW;
				break;
			}
		}

		/* Check for card already found */
		if (j < hand_size) continue;

		/* Get card pointer */
		c_ptr = &real_game.deck[list[i]];

		/* Get next entry in hand list */
		i_ptr = &hand[hand_size++];

		/* Reset structure */
		reset_display(i_ptr);

		/* Add card information */
		i_ptr->index = list[i];
		i_ptr->d_ptr = c_ptr->d_ptr;

		/* Card is in hand */
		i_ptr->hand = 1;

		/* Card is eligible for selection */
		i_ptr->eligible = 1;
		i_ptr->greedy = 1;

		/* Display card with gap */
		i_ptr->gapped = 1;

		/* Card should be highlighted when selected */
		i_ptr->highlight = HIGH_YELLOW;
		i_ptr->highlight_else = HIGH_RED;

		/* Set tool tip */
		i_ptr->tooltip = card_hand_tooltip(g, who, list[i]);
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

	/* Clear special cards */
	num_special_cards = 0;

	/* Set number of cards selected */
	*num = n;
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
	sprintf(buf, "Choose card to discard for prestige");

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
				i_ptr->greedy = 1;

				/* Highlight in red when selected */
				i_ptr->highlight = HIGH_RED;

				/* Card should be pushed up when selected */
				i_ptr->push = 1;
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
	int i, j, n, allow_takeover = (phase == PHASE_SETTLE);
	power_where w_list[100];
	power *o_ptr;

	/* Create prompt */
	sprintf(buf, "Choose card to %s",
	        phase == PHASE_DEVELOP ? "develop" : "settle");

	/* Check for special card used to provide power */
	if (special != -1)
	{
		/* Append name to prompt */
		strcat(buf, " using ");
		strcat(buf, g->deck[special].d_ptr->name);

		/* XXX Check for "Rebel Sneak Attack" */
		if (!strcmp(g->deck[special].d_ptr->name, "Rebel Sneak Attack"))
		{
			/* Takeover not allowed */
			allow_takeover = FALSE;
		}
	}

	/* Check for settle phase and possible takeover */
	if (allow_takeover && settle_check_takeover(g, who, NULL, 1))
	{
		/* Append takeover information */
		strcat(buf, " (or pass if you want to perform a takeover)");
	}

	/* Check for possible flip power */
	if (phase == PHASE_SETTLE)
	{
		/* Get settle powers */
		n = get_powers(g, who, PHASE_SETTLE, w_list);

		/* Loop over powers */
		for (i = 0; i < n; i++)
		{
			/* Get power pointer */
			o_ptr = w_list[i].o_ptr;

			/* Skip powers that aren't "flip for zero" */
			if (!(o_ptr->code & P3_FLIP_ZERO)) continue;

			/* Append flip information */
			strcat(buf, " (or pass if you want to flip a card)");

			/* Done */
			break;
		}
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
				i_ptr->greedy = 1;

				/* Card should be highlighted when selected */
				i_ptr->highlight = HIGH_YELLOW;

				/* Check for develop phase */
				if (opt.cost_in_hand && phase == PHASE_DEVELOP)
				{
					/* Set develop tool tip */
					i_ptr->tooltip = card_develop_tooltip(g, player_us, i_ptr);
				}

				/* Check for settle phase */
				else if (opt.cost_in_hand && phase == PHASE_SETTLE)
				{
					/* Set settle tool tip */
					i_ptr->tooltip = card_settle_tooltip(g, player_us, special,
					                                     i_ptr);
				}
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
 * Find if there are any forced choices for payment.
 */
static void compute_forced_choice(int list[], int num,
                                  int special[], int num_special,
                                  long *special_mask, int *forced_hand, int mil_bonus)
{
	game sim;
	int i, j, num_choice;
	int special_choice[MAX_DECK];
	long special_set;

	/* Clear forced variables */
	*special_mask = ~0;
	*forced_hand = TRUE;

	/* Loop over all reasonable hand payments */
	for (i = 0; i <= num && i < 20; ++i)
	{
		/* Loop over all sets of special cards */
		for (special_set = 0; special_set < (1 << num_special); ++special_set)
		{
			/* Clear number of special cards used */
			num_choice = 0;

			/* Loop over special cards */
			for (j = 0; j < num_special; ++j)
			{
				/* Check for this special card selected */
				if (special_set & (1 << j))
				{
					/* Add card */
					special_choice[num_choice++] = special[j];
				}
			}

			/* Copy game */
			sim = real_game;

			/* Set simulation flag */
			sim.simulation = 1;

			/* Loop over players */
			for (j = 0; j < sim.num_players; j++)
			{
				/* Have AI make any pending decisions for this player */
				sim.p[j].control = &ai_func;
			}

			/* Try to make payment */
			if (payment_callback(&sim, player_us, action_payment_which,
			                     list, i, special_choice, num_choice,
			                     action_payment_mil, mil_bonus))
			{
				/* Check for legal without all hand cards */
				if (i != num) *forced_hand = FALSE;

				/* Update mask */
				*special_mask &= special_set;
			}

			/* Optimization */
			if (!special_mask && !forced_hand) return;
		}
	}
}


/*
 * Choose method of payment for a placed card.
 *
 * We include some active cards that have powers that can be triggered,
 * such as the Contact Specialist or Colony Ship.
 */
void gui_choose_pay(game *g, int who, int which, int list[], int *num,
                    int special[], int *num_special, int mil_only,
                    int mil_bonus)
{
	card *c_ptr;
	displayed *i_ptr;
	power *o_ptr;
	char *cost_card;
	char buf[1024], *p;
	int i, j, n = 0, ns = 0, high_color;
	int military, cost, ict_mil, iif_mil;
	int forced_hand;
	long forced_special;

	/* Get card we are paying for */
	c_ptr = &real_game.deck[which];

	/* Reset displayed cards */
	reset_cards(g, FALSE, FALSE);

	/* Start at beginning of buffer */
	p = buf;

	/* Create prompt */
	p += sprintf(p, "Choose payment for %s ", c_ptr->d_ptr->name);

	/* Check for development */
	if (c_ptr->d_ptr->type == TYPE_DEVELOPMENT)
	{
		/* Compute cost */
		cost = devel_cost(g, who, which);

		/* Create prompt */
		p += sprintf(p, "(%d card%s)", cost, PLURAL(cost));
	}

	/* Check for world */
	else if (c_ptr->d_ptr->type == TYPE_WORLD)
	{
		/* Check for takeover */
		if (c_ptr->owner != who)
		{
			/* Compute strength difference */
			military =
				strength_against(g, who, which,
				                 g->takeover_power[g->num_takeover - 1], 0) -
				strength_against(g, c_ptr->owner, which, -1, 1);

			/* Check for ahead in strength */
			if (military > 0)
			{
				/* Format text */
				p += sprintf(p, "(currently %d military ahead)", military);
			}

			/* Check for equal strength */
			else if (military == 0)
			{
				/* Format text */
				p += sprintf(p, "(currently equal strength)");
			}

			/* Behind in strength */
			else
			{
				/* Format text */
				p += sprintf(p, "(currently %d military behind)", -military);
			}
		}

		/* Check for military world */
		else if (c_ptr->d_ptr->flags & FLAG_MILITARY)
		{
			/* Compute payment */
			military_world_payment(g, who, which, mil_only, mil_bonus,
			                       &status_player[who].discount,
			                       &military, &cost, &cost_card);

			/* Check for no pay-for-military power */
			if (cost == -1)
			{
				/* Format text */
				p += sprintf(p, "(%d military)", military);
			}
			else
			{
				/* Format text */
				p += sprintf(p, "(%d military or %d card%s)",
				             military, cost, PLURAL(cost));
			}
		}
		else
		{
			/* Compute payment */
			peaceful_world_payment(g, who, which, mil_only,
			                       &status_player[who].discount,
			                       &cost, &ict_mil, &iif_mil);

			/* Format text */
			p += sprintf(p, "(");

			/* Check for cost available */
			if (cost >= 0)
			{
				/* Format text */
				p += sprintf(p, "%d card%s", cost, PLURAL(cost));
			}

			/* Check for ICT or IIF */
			if (ict_mil >= 0 || iif_mil >= 0)
			{
				/* Check for cost */
				if (cost >= 0) p += sprintf(p, " or ");

				/* Check for both ICT and IIF and different military needed */
				if (ict_mil >= 0 && iif_mil >= 0 && ict_mil != iif_mil)
				{
					/* Format text */
					p += sprintf(p, "%d/%d military", ict_mil, iif_mil);
				}

				/* Check for only ICT, or equal military needed */
				else if (ict_mil >= 0)
				{
					/* Format text */
					p += sprintf(p, "%d military", ict_mil);
				}

				/* Check for only IIF */
				else if (iif_mil >= 0)
				{
					/* Format text */
					p += sprintf(p, "%d military", iif_mil);
				}
			}

			/* Format text */
			p += sprintf(p, ")");
		}
	}

	/* Set prompt */
	gtk_label_set_text(GTK_LABEL(action_prompt), buf);

	/* Set button restriction */
	action_restrict = RESTRICT_PAY;
	action_payment_which = which;
	action_payment_mil = mil_only;
	action_payment_bonus = mil_bonus;

	/* Check for auto-selecting forced choices */
	if (opt.auto_select)
	{
		/* Find any forced choices */
		compute_forced_choice(list, *num, special, *num_special,
		                      &forced_special, &forced_hand, mil_bonus);
	}
	else
	{
		/* No forced payment */
		forced_hand = forced_special = 0;
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

				/* Card should be red when selected */
				i_ptr->highlight = HIGH_RED;

				/* Card should be pushed up when selected */
				i_ptr->push = 1;

				/* Check for forced choice */
				if (forced_hand) i_ptr->selected = TRUE;
			}
		}
	}

	/* Loop over special cards */
	for (i = 0; i < *num_special; i++)
	{
		/* Assume highlight color will be yellow */
		high_color = HIGH_YELLOW;

		/* Loop over powers on card */
		for (j = 0; j < g->deck[special[i]].d_ptr->num_power; j++)
		{
			/* Get power pointer */
			o_ptr = &g->deck[special[i]].d_ptr->powers[j];

			/* Skip non-develop or settle powers */
			if (o_ptr->phase != PHASE_DEVELOP &&
			    o_ptr->phase != PHASE_SETTLE) continue;

			/* Check for discard in develop phase */
			if (o_ptr->phase == PHASE_DEVELOP &&
			    (o_ptr->code & P2_DISCARD_REDUCE))
				high_color = HIGH_RED;

			/* Check for discard in settle phase */
			if (o_ptr->phase == PHASE_SETTLE &&
			    (o_ptr->code & P3_DISCARD))
				high_color = HIGH_RED;
		}

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
				i_ptr->highlight = high_color;

				/* Check for forced choice */
				if (forced_special & (1 << i)) i_ptr->selected = TRUE;
			}
		}
	}

	/* (De)activate action button */
	gtk_widget_set_sensitive(action_button, action_check_payment());

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
 * Choose a settle power to use.
 */
void gui_choose_settle(game *g, int who, int cidx[], int oidx[], int *num,
                       int *num_special)
{
	GtkWidget *combo;
	card *c_ptr;
	power *o_ptr;
	pow_loc l_list[MAX_DECK];
	char buf[1024];
	int i;

	/* Activate action button */
	gtk_widget_set_sensitive(action_button, TRUE);

	/* Reset displayed cards */
	reset_cards(g, TRUE, TRUE);

	/* Redraw everything */
	redraw_everything();

	/* Set prompt */
	gtk_label_set_text(GTK_LABEL(action_prompt), "Choose Settle power");

	/* Create simple combo box */
	combo = gtk_combo_box_new_text();

	/* Loop over powers */
	for (i = 0; i < *num; i++)
	{
		/* Create power location */
		l_list[i].c_idx = cidx[i];
		l_list[i].o_idx = oidx[i];
	}

	/* Loop over powers */
	for (i = 0; i < *num; i++)
	{
		/* Get card pointer */
		c_ptr = &g->deck[l_list[i].c_idx];

		/* Get power pointer */
		o_ptr = &c_ptr->d_ptr->powers[l_list[i].o_idx];

		/* Check for simple powers */
		if (o_ptr->code & P3_PLACE_TWO)
		{
			/* Make string */
			sprintf(buf, "Place second world");
		}
		else if (o_ptr->code & P3_PLACE_MILITARY)
		{
			/* Make string */
			sprintf(buf, "Place second military world");
		}
		else if (o_ptr->code & P3_PLACE_LEFTOVER)
		{
			/* Make string */
			sprintf(buf, "Place with leftover military");
		}
		else if (o_ptr->code & P3_UPGRADE_WORLD)
		{
			/* Make string */
			sprintf(buf, "Upgrade world");
		}
		else if (o_ptr->code & P3_PLACE_ZERO)
		{
			/* Make string */
			sprintf(buf, "Place non-military world at zero cost");
		}
		else if (o_ptr->code & P3_FLIP_ZERO)
		{
			/* Make string */
			sprintf(buf, "Flip to place non-military world");
		}

		/* Append option to combo box */
		gtk_combo_box_append_text(GTK_COMBO_BOX(combo), buf);
	}

	/* Append no choice option */
	sprintf(buf, "None (done with Settle)");

	/* Append option to combo box */
	gtk_combo_box_append_text(GTK_COMBO_BOX(combo), buf);

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
		*num = *num_special = 0;
		return;
	}

	/* Select chosen power */
	cidx[0] = l_list[i].c_idx;
	oidx[0] = l_list[i].o_idx;
	*num = *num_special = 1;
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
	power *o_ptr;
	char buf[1024];
	int i, j, k, target = -1, high_color;

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

	/* Loop over special cards */
	for (i = 0; i < *num_special; i++)
	{
		/* Assume highlight color will be yellow */
		high_color = HIGH_YELLOW;

		/* Loop over powers on card */
		for (j = 0; j < g->deck[special[i]].d_ptr->num_power; j++)
		{
			/* Get power pointer */
			o_ptr = &g->deck[special[i]].d_ptr->powers[j];

			/* Skip non-settle powers */
			if (o_ptr->phase != PHASE_SETTLE) continue;

			/* Skip non-takeover powers */
			if (!(o_ptr->code & (P3_TAKEOVER_REBEL |
			                     P3_TAKEOVER_IMPERIUM |
			                     P3_TAKEOVER_MILITARY |
			                     P3_TAKEOVER_PRESTIGE))) continue;

			/* Check for discard to use power */
			if (o_ptr->code & P3_DISCARD) high_color = HIGH_RED;
		}

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
				i_ptr->highlight = high_color;
			}
		}
	}

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
					i_ptr->highlight = HIGH_YELLOW;
					i_ptr->tooltip = card_takeover_tooltip(g, j, player_us,
					                                       i_ptr);
				}
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
	power *o_ptr;
	char buf[1024];
	int i, j, n = 0, ns = 0, high_color;

	/* Get card we are defending */
	c_ptr = &real_game.deck[which];

	/* Create prompt */
	sprintf(buf, "Choose defense for %s (need %d extra military)",
	        c_ptr->d_ptr->name, deficit + 1);

	/* Set prompt */
	gtk_label_set_text(GTK_LABEL(action_prompt), buf);

	/* Reset displayed cards */
	reset_cards(g, FALSE, FALSE);

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

				/* Highlight card in red when selected */
				i_ptr->highlight = HIGH_RED;

				/* Card should be pushed up when selected */
				i_ptr->push = 1;
			}
		}
	}

	/* Loop over special cards */
	for (i = 0; i < *num_special; i++)
	{
		/* Assume highlight color will be yellow */
		high_color = HIGH_YELLOW;

		/* Loop over powers on card */
		for (j = 0; j < g->deck[special[i]].d_ptr->num_power; j++)
		{
			/* Get power pointer */
			o_ptr = &g->deck[special[i]].d_ptr->powers[j];

			/* Skip non-settle powers */
			if (o_ptr->phase != PHASE_SETTLE) continue;

			/* Check for discard to use power */
			if (o_ptr->code & P3_DISCARD) high_color = HIGH_RED;
		}

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
				i_ptr->highlight = high_color;
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
                                 int special[], int *num_special)
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

	/* Add handler for keypresses */
	gtk_widget_add_accelerator(combo, "key-signal", window_accel,
	                           GDK_F12, 0, 0);
	gtk_widget_add_accelerator(combo, "up-signal", window_accel,
	                           GDK_Up, GDK_SHIFT_MASK, 0);
	gtk_widget_add_accelerator(combo, "down-signal", window_accel,
	                           GDK_Down, GDK_SHIFT_MASK, 0);

	/* Connect key signals */
	g_signal_connect(G_OBJECT(combo), "key-signal",
	                 G_CALLBACK(combo_open), NULL);
	g_signal_connect(G_OBJECT(combo), "up-signal",
	                 G_CALLBACK(combo_up), NULL);
	g_signal_connect(G_OBJECT(combo), "down-signal",
	                 G_CALLBACK(combo_down), GINT_TO_POINTER(*num + 1));

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
		*num = *num_special = 0;
		return;
	}

	/* Select takeover to prevent */
	list[0] = list[i];
	special[0] = special[i];
	*num = *num_special = 1;
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
				i_ptr->greedy = 1;

				/* Card should be highlighted when selected */
				i_ptr->highlight = HIGH_YELLOW;
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
				i_ptr->greedy = 1;

				/* Card should be highlighted when selected */
				i_ptr->highlight = HIGH_RED;
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
	int forced = opt.auto_select && *num == 1;

	/* Create prompt */
	sprintf(buf, "Choose good to trade%s", no_bonus ? " (no bonuses)" : "");

	/* Set prompt */
	gtk_label_set_text(GTK_LABEL(action_prompt), buf);

	/* Set restrictions on action button */
	action_restrict = RESTRICT_NUM;
	action_min = action_max = 1;

	/* (De)activate action button */
	gtk_widget_set_sensitive(action_button, forced);

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
				i_ptr->greedy = 1;
				i_ptr->tooltip = card_trade_tooltip(g, player_us, i_ptr, no_bonus);

				/* Push good upwards when selected */
				i_ptr->push = 1;

				/* Check for forced choice */
				if (forced) i_ptr->selected = TRUE;
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
 * Return a "score" for sorting consume powers.
 */
static int score_consume(power *o_ptr)
{
	int vp = 0, card = 0, prestige = 0, goods = 1;
	int vp_mult = 1, score = 0;

	/* Check for discard form hand */
	if (o_ptr->code & P4_DISCARD_HAND)
	{
		/* Always discard from hand last */
		score -= 1000;

		/* Check for VP awarded */
		if (o_ptr->code & P4_GET_VP) vp += o_ptr->value;

		/* Check for card awarded */
		if (o_ptr->code & P4_GET_CARD) card += o_ptr->value;

		/* Check for prestige awarded */
		if (o_ptr->code & P4_GET_PRESTIGE) prestige += o_ptr->value;

		/* Check for consuming two cards */
		if (o_ptr->code & P4_CONSUME_TWO) goods = 2;

		/* Compute score */
		score += (card * 150 + prestige * 100 + vp * 75) / goods;

		/* Use multi-use powers later */
		if (o_ptr->times > 1) score -= 2 * o_ptr->times;

		/* Return score */
		return score;
	}

	/* Check for consume prestige */
	if (o_ptr->code & P4_CONSUME_PRESTIGE)
	{
		/* Consume prestige next to last */
		score -= 500;

		/* Check for VP awarded */
		if (o_ptr->code & P4_GET_VP) score += o_ptr->value * 2;

		/* Check for cards awarded */
		if (o_ptr->code & P4_GET_CARD) score += o_ptr->value;

		/* Return score */
		return score;
	}

	/* Check for free VP */
	if (o_ptr->code & P4_VP) return o_ptr->value * 1000;

	/* Check for free card draw */
	if (o_ptr->code & P4_DRAW) return o_ptr->value * 750;

	/* Check for VP awarded */
	if (o_ptr->code & P4_GET_VP) vp += o_ptr->value;

	/* Check for card awarded */
	if (o_ptr->code & P4_GET_CARD) card += o_ptr->value;

	/* Check for cards awarded */
	if (o_ptr->code & P4_GET_2_CARD) card += o_ptr->value * 2;
	if (o_ptr->code & P4_GET_3_CARD) card += o_ptr->value * 3;

	/* Check for prestige awarded */
	if (o_ptr->code & P4_GET_PRESTIGE) prestige += o_ptr->value;

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

	/* Check for double VP action */
	if (player_chose(&real_game, player_us, ACT_CONSUME_X2) ||
	    player_chose(&real_game, player_us, ACT_CONSUME_TRADE | ACT_PRESTIGE))
	{
		/* Multiplier is two */
		vp_mult = 2;
	}

	/* Check for triple VP action */
	if (player_chose(&real_game, player_us, ACT_PRESTIGE | ACT_CONSUME_X2))
	{
		/* Multiplier is three */
		vp_mult = 3;
	}

	/* Compute score */
	score = (prestige * 150 + vp * vp_mult * 100 + card * 52) / goods;

	/* Use specific consume powers first */
	if (!(o_ptr->code & P4_CONSUME_ANY)) score += 10;

	/* Use multi-use powers later */
	if (o_ptr->times > 1) score -= 2 * o_ptr->times;

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
	power *o_ptr1, *o_ptr2, bonus;

	/* Check first power */
	if (l_ptr1->c_idx < 0)
	{
		/* Use bonus power */
		bonus.phase = PHASE_CONSUME;
		bonus.code = P4_DISCARD_HAND | P4_GET_VP;
		bonus.value = 1;
		bonus.times = 2;

		/* Use fake power */
		o_ptr1 = &bonus;
	}
	else
	{
		/* Get power */
		o_ptr1 = &real_game.deck[l_ptr1->c_idx].d_ptr->powers[l_ptr1->o_idx];
	}

	/* Check second power */
	if (l_ptr2->c_idx < 0)
	{
		/* Use bonus power */
		bonus.phase = PHASE_CONSUME;
		bonus.code = P4_DISCARD_HAND | P4_GET_VP;
		bonus.value = 1;
		bonus.times = 2;

		/* Use fake power */
		o_ptr2 = &bonus;
	}
	else
	{
		/* Get power */
		o_ptr2 = &real_game.deck[l_ptr2->c_idx].d_ptr->powers[l_ptr2->o_idx];
	}

	/* Compare consume powers */
	return score_consume(o_ptr2) - score_consume(o_ptr1);
}

/*
 * Ask user which consume power to use.
 */
void gui_choose_consume(game *g, int who, int cidx[], int oidx[], int *num,
                        int *num_special, int optional)
{
	GtkWidget *combo;
	card *c_ptr;
	power *o_ptr, prestige_bonus;
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
		/* Check for prestige trade bonus power */
		if (l_list[i].c_idx < 0)
		{
			/* Make fake power */
			prestige_bonus.phase = PHASE_CONSUME;
			prestige_bonus.code = P4_DISCARD_HAND | P4_GET_VP;
			prestige_bonus.value = 1;
			prestige_bonus.times = 2;

			/* Use fake power */
			o_ptr = &prestige_bonus;
		}
		else
		{
			/* Get card pointer */
			c_ptr = &g->deck[l_list[i].c_idx];

			/* Get power pointer */
			o_ptr = &c_ptr->d_ptr->powers[l_list[i].o_idx];
		}

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
				/* Genes good */
				name = "Genes ";
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
				sprintf(buf2, "%d card%s", o_ptr->value, PLURAL(o_ptr->value));

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

			/* Check for extra cards */
			if (o_ptr->code & P4_GET_3_CARD)
			{
				/* Create card reward string */
				strcat(buf, "3 cards");

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

	/* Add handler for keypresses */
	gtk_widget_add_accelerator(combo, "key-signal", window_accel,
	                           GDK_F12, 0, 0);
	gtk_widget_add_accelerator(combo, "up-signal", window_accel,
	                           GDK_Up, GDK_SHIFT_MASK, 0);
	gtk_widget_add_accelerator(combo, "down-signal", window_accel,
	                           GDK_Down, GDK_SHIFT_MASK, 0);

	/* Connect key signals */
	g_signal_connect(G_OBJECT(combo), "key-signal",
	                 G_CALLBACK(combo_open), NULL);
	g_signal_connect(G_OBJECT(combo), "up-signal",
	                 G_CALLBACK(combo_up), NULL);
	g_signal_connect(G_OBJECT(combo), "down-signal",
	                 G_CALLBACK(combo_down), GINT_TO_POINTER(*num + optional));

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
		*num = *num_special = 0;
		return;
	}

	/* Select chosen power */
	cidx[0] = l_list[i].c_idx;
	oidx[0] = l_list[i].o_idx;
	*num = *num_special = 1;
}

/*
 * Consume cards from hand.
 */
void gui_choose_consume_hand(game *g, int who, int c_idx, int o_idx, int list[],
                             int *num)
{
	card *c_ptr;
	power *o_ptr, prestige_bonus;
	char buf[1024], *card_name;
	displayed *i_ptr;
	int i, j, n = 0;

	/* Check for prestige trade bonus power */
	if (c_idx < 0)
	{
		/* Make fake power */
		prestige_bonus.phase = PHASE_CONSUME;
		prestige_bonus.code = P4_DISCARD_HAND | P4_GET_VP;
		prestige_bonus.value = 1;
		prestige_bonus.times = 2;

		/* Use fake power */
		o_ptr = &prestige_bonus;

		/* Use fake card name */
		card_name = "Prestige Trade bonus";
	}
	else
	{
		/* Get card pointer */
		c_ptr = &g->deck[c_idx];

		/* Get power pointer */
		o_ptr = &c_ptr->d_ptr->powers[o_idx];

		/* Use card name */
		card_name = c_ptr->d_ptr->name;
	}

	/* Check for needing two cards */
	if (o_ptr->code & P4_CONSUME_TWO)
	{
		/* Create prompt */
		sprintf(buf, "Choose cards to consume on %s", card_name);
	}
	else
	{
		/* Create prompt */
		sprintf(buf, "Choose up to %d card%s to consume on %s",
		        o_ptr->times, PLURAL(o_ptr->times), card_name);
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

				/* Card should be red when selected */
				i_ptr->highlight = HIGH_RED;

				/* Card should be pushed up when selected */
				i_ptr->push = 1;
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
	int i, j, n = 0, multi = -1;
	int forced = opt.auto_select && min == max && min == *num;

	/* Get pointer to card holding consume power */
	c_ptr = &real_game.deck[c_idx];

	/* Create prompt */
	sprintf(buf, "Choose good%s to consume on %s",
	        min == 1 && max == 1 ? "" : "s", c_ptr->d_ptr->name);

	/* Set prompt */
	gtk_label_set_text(GTK_LABEL(action_prompt), buf);

	/* Set restrictions on action button */
	action_restrict = RESTRICT_GOOD;
	action_min = min;
	action_max = max;
	action_cidx = c_idx;
	action_oidx = o_idx;

	/* (De)activate action button */
	gtk_widget_set_sensitive(action_button, min == 0 || forced);

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

				/* Push good upwards when selected */
				i_ptr->push = 1;

				/* Check for forced choice */
				if (forced) i_ptr->selected = TRUE;

				/* Check for multiple goods */
				if (i_ptr->num_goods > 1) multi = j;
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

	/* Check for not enough goods */
	if (n < min)
	{
		/* Get displayed card pointer */
		i_ptr = &table[player_us][multi];

		/* Add enough goods */
		while (n < min) goods[n++] = i_ptr->index;
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
				i_ptr->greedy = 1;

				/* Card should be pushed up when selected */
				i_ptr->push = 1;
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

	/* Save special cards */
	num_special_cards = num;
	for (i = 0; i < num; ++i) special_cards[i] = &g->deck[list[i]];

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

		/* Reset structure */
		reset_display(i_ptr);

		/* Add card information */
		i_ptr->index = list[i];
		i_ptr->d_ptr = c_ptr->d_ptr;

		/* Card is in hand */
		i_ptr->hand = 1;

		/* Card is eligible */
		i_ptr->eligible = 1;
		i_ptr->gapped = 1;
		i_ptr->greedy = 1;

		/* Highlight card when selected */
		i_ptr->highlight = HIGH_YELLOW;
		i_ptr->highlight_else = HIGH_RED;

		/* Set tool tip */
		i_ptr->tooltip = card_hand_tooltip(g, who, list[i]);
	}

	/* Redraw everything */
	redraw_everything();

	/* Process events */
	gtk_main();

	/* Clear special cards */
	num_special_cards = 0;

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
	int forced = opt.auto_select && *num == 1;

	/* Create prompt */
	sprintf(buf, "Choose windfall world to produce");

	/* Set prompt */
	gtk_label_set_text(GTK_LABEL(action_prompt), buf);

	/* Set restrictions on action button */
	action_restrict = RESTRICT_NUM;
	action_min = action_max = 1;

	/* Deactivate action button */
	gtk_widget_set_sensitive(action_button, forced);

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

				/* Only one card can be selected */
				i_ptr->greedy = 1;

				/* Push good upwards when selected */
				i_ptr->push = 1;

				/* Check for forced choice */
				if (forced) i_ptr->selected = TRUE;
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
 * Return a "score" for sorting produce powers.
 */
static int score_produce(power *o_ptr)
{
	int score = 0;

	/* List non-discard powers first */
	if (!(o_ptr->code & P5_DISCARD)) score += 10;

	/* Score not this slightly above */
	if (o_ptr->code & P5_NOT_THIS) score += 1;

	/* Score specific powers before generic */
	if (o_ptr->code & P5_WINDFALL_NOVELTY) score += 8;
	if (o_ptr->code & P5_WINDFALL_RARE) score += 6;
	if (o_ptr->code & P5_WINDFALL_GENE) score += 4;
	if (o_ptr->code & P5_WINDFALL_ALIEN) score += 2;

	/* List draw powers last */
	if (o_ptr->code & P5_DRAW_EACH_NOVELTY) score = -2;
	if (o_ptr->code & P5_DRAW_EACH_RARE) score = -4;
	if (o_ptr->code & P5_DRAW_EACH_GENE) score = -6;
	if (o_ptr->code & P5_DRAW_EACH_ALIEN) score = -8;
	if (o_ptr->code & P5_DRAW_DIFFERENT) score = -10;

	/* Return score */
	return score;
}

/*
 * Compare two produce powers for sorting.
 */
static int cmp_produce(const void *l1, const void *l2)
{
	pow_loc *l_ptr1 = (pow_loc *)l1;
	pow_loc *l_ptr2 = (pow_loc *)l2;
	power *o_ptr1;
	power *o_ptr2;
	power bonus;

	/* Check first power */
	if (l_ptr1->c_idx < 0)
	{
		/* Use bonus power */
		bonus.code = P5_WINDFALL_ANY;
		o_ptr1 = &bonus;
	}
	else
	{
		/* Get power */
		o_ptr1 = &real_game.deck[l_ptr1->c_idx].d_ptr->powers[l_ptr1->o_idx];
	}

	/* Check second power */
	if (l_ptr2->c_idx < 0)
	{
		/* Use bonus power */
		bonus.code = P5_WINDFALL_ANY;
		o_ptr2 = &bonus;
	}
	else
	{
		/* Get power */
		o_ptr2 = &real_game.deck[l_ptr2->c_idx].d_ptr->powers[l_ptr2->o_idx];
	}

	/* Compare produce powers */
	return score_produce(o_ptr2) - score_produce(o_ptr1);
}

/*
 * Choose a produce power to use.
 */
void gui_choose_produce(game *g, int who, int cidx[], int oidx[], int num)
{
	GtkWidget *combo;
	card *c_ptr = NULL;
	power *o_ptr, bonus;
	pow_loc l_list[MAX_DECK];
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
		/* Create power location */
		l_list[i].c_idx = cidx[i];
		l_list[i].o_idx = oidx[i];
	}

	/* Sort produce powers */
	qsort(l_list, num, sizeof(pow_loc), cmp_produce);

	/* Loop over powers */
	for (i = 0; i < num; i++)
	{
		/* Check for produce or prestige bonus */
		if (l_list[i].c_idx < 0)
		{
			/* Create fake produce power */
			bonus.code = P5_WINDFALL_ANY;
			o_ptr = &bonus;
		}
		else
		{
			/* Get card pointer */
			c_ptr = &g->deck[l_list[i].c_idx];

			/* Get power pointer */
			o_ptr = &c_ptr->d_ptr->powers[l_list[i].o_idx];
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
			sprintf(buf, "Draw per Genes produced");
		}
		else if (o_ptr->code & P5_DRAW_EACH_ALIEN)
		{
			/* Make string */
			sprintf(buf, "Draw per Alien produced");
		}
		else if (o_ptr->code & P5_DRAW_DIFFERENT)
		{
			/* Make string */
			sprintf(buf, "Draw per kind produced");
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
		else if ((o_ptr->code & P5_WINDFALL_GENE) &&
		         (o_ptr->code & P5_NOT_THIS))
		{
			/* Add to string */
			strcat(buf, "produce on other Genes windfall");
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

	/* Add handler for keypresses */
	gtk_widget_add_accelerator(combo, "key-signal", window_accel,
	                           GDK_F12, 0, 0);
	gtk_widget_add_accelerator(combo, "up-signal", window_accel,
	                           GDK_Up, GDK_SHIFT_MASK, 0);
	gtk_widget_add_accelerator(combo, "down-signal", window_accel,
	                           GDK_Down, GDK_SHIFT_MASK, 0);

	/* Connect key signals */
	g_signal_connect(G_OBJECT(combo), "key-signal",
	                 G_CALLBACK(combo_open), NULL);
	g_signal_connect(G_OBJECT(combo), "up-signal",
	                 G_CALLBACK(combo_up), NULL);
	g_signal_connect(G_OBJECT(combo), "down-signal",
	                 G_CALLBACK(combo_down), GINT_TO_POINTER(num));

	/* Show everything */
	gtk_widget_show_all(combo);

	/* Process events */
	gtk_main();

	/* Get selection */
	i = gtk_combo_box_get_active(GTK_COMBO_BOX(combo));

	/* Destroy combo box */
	gtk_widget_destroy(combo);

	/* Select chosen power */
	cidx[0] = l_list[i].c_idx;
	oidx[0] = l_list[i].o_idx;
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
				i_ptr->greedy = 1;

				/* Card should be red when selected */
				i_ptr->highlight = HIGH_RED;

				/* Card should be pushed up when selected */
				i_ptr->push = 1;
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
				i_ptr->highlight = HIGH_YELLOW;

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

	/* Add handler for keypresses */
	gtk_widget_add_accelerator(combo, "key-signal", window_accel,
	                           GDK_F12, 0, 0);
	gtk_widget_add_accelerator(combo, "up-signal", window_accel,
	                           GDK_Up, GDK_SHIFT_MASK, 0);
	gtk_widget_add_accelerator(combo, "down-signal", window_accel,
	                           GDK_Down, GDK_SHIFT_MASK, 0);

	/* Connect key signals */
	g_signal_connect(G_OBJECT(combo), "key-signal",
	                 G_CALLBACK(combo_open), NULL);
	g_signal_connect(G_OBJECT(combo), "up-signal",
	                 G_CALLBACK(combo_up), NULL);
	g_signal_connect(G_OBJECT(combo), "down-signal",
	                 G_CALLBACK(combo_down),
	                 GINT_TO_POINTER(MAX_SEARCH - real_game.takeover_disabled));

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
	GtkWidget *combo;
	card *c_ptr;
	displayed *i_ptr;
	char buf[1024];
	int i;

	/* Save special card */
	num_special_cards = 1;
	special_cards[0] = &g->deck[arg1];

	/* Get card pointer */
	c_ptr = &g->deck[arg1];

	/* Create prompt */
	sprintf(buf, "Choose to keep/discard %s", c_ptr->d_ptr->name);

	/* Set prompt */
	gtk_label_set_text(GTK_LABEL(action_prompt), buf);

	/* Reset displayed cards */
	reset_cards(g, TRUE, TRUE);

	/* Activate action button */
	gtk_widget_set_sensitive(action_button, TRUE);

	/* Get next entry in hand list */
	i_ptr = &hand[hand_size++];

	/* Reset structure */
	reset_display(i_ptr);

	/* Add card information */
	i_ptr->index = arg1;
	i_ptr->d_ptr = c_ptr->d_ptr;

	/* Set tool tip */
	i_ptr->tooltip = card_hand_tooltip(g, who, arg1);

	/* Card is in hand */
	i_ptr->hand = 1;

	/* Card should be separated from hand */
	i_ptr->gapped = 1;
	i_ptr->color = 1;

	/* Create simple combo box */
	combo = gtk_combo_box_new_text();

	/* Append options to combo box */
	gtk_combo_box_append_text(GTK_COMBO_BOX(combo),
	                          "Discard (keep searching)");
	gtk_combo_box_append_text(GTK_COMBO_BOX(combo), "Keep card");

	/* Set first choice */
	gtk_combo_box_set_active(GTK_COMBO_BOX(combo), 0);

	/* Add combo box to action box */
	gtk_box_pack_end(GTK_BOX(action_box), combo, FALSE, TRUE, 0);

	/* Add handler for keypresses */
	gtk_widget_add_accelerator(combo, "key-signal", window_accel,
	                           GDK_F12, 0, 0);
	gtk_widget_add_accelerator(combo, "up-signal", window_accel,
	                           GDK_Up, GDK_SHIFT_MASK, 0);
	gtk_widget_add_accelerator(combo, "down-signal", window_accel,
	                           GDK_Down, GDK_SHIFT_MASK, 0);

	/* Connect key signals */
	g_signal_connect(G_OBJECT(combo), "key-signal",
	                 G_CALLBACK(combo_open), NULL);
	g_signal_connect(G_OBJECT(combo), "up-signal",
	                 G_CALLBACK(combo_up), NULL);
	g_signal_connect(G_OBJECT(combo), "down-signal",
	                 G_CALLBACK(combo_down), GINT_TO_POINTER(2));

	/* Show everything */
	gtk_widget_show_all(combo);

	/* Redraw everything */
	redraw_everything();

	/* Process events */
	gtk_main();

	/* Clear special cards */
	num_special_cards = 0;

	/* Get selection */
	i = gtk_combo_box_get_active(GTK_COMBO_BOX(combo));

	/* Destroy combo box */
	gtk_widget_destroy(combo);

	/* Return choice */
	return i;
}

/*
 * Ask player to choose color of Alien Oort Cloud Refinery
 */
int gui_choose_oort_kind(game *g, int who)
{
	GtkWidget *combo;
	char buf[1024];
	int i;

	/* Create prompt */
	sprintf(buf, "Choose Alien Oort Cloud Refinery kind");

	/* Set prompt */
	gtk_label_set_text(GTK_LABEL(action_prompt), buf);

	/* Reset displayed cards */
	reset_cards(g, TRUE, TRUE);

	/* Activate action button */
	gtk_widget_set_sensitive(action_button, TRUE);

	/* Create simple combo box */
	combo = gtk_combo_box_new_text();

	/* Append options to combo box */
	gtk_combo_box_append_text(GTK_COMBO_BOX(combo), "Novelty");
	gtk_combo_box_append_text(GTK_COMBO_BOX(combo), "Rare");
	gtk_combo_box_append_text(GTK_COMBO_BOX(combo), "Genes");
	gtk_combo_box_append_text(GTK_COMBO_BOX(combo), "Alien");

	/* Set first choice */
	gtk_combo_box_set_active(GTK_COMBO_BOX(combo), 0);

	/* Add combo box to action box */
	gtk_box_pack_end(GTK_BOX(action_box), combo, FALSE, TRUE, 0);

	/* Add handler for keypresses */
	gtk_widget_add_accelerator(combo, "key-signal", window_accel,
	                           GDK_F12, 0, 0);
	gtk_widget_add_accelerator(combo, "up-signal", window_accel,
	                           GDK_Up, GDK_SHIFT_MASK, 0);
	gtk_widget_add_accelerator(combo, "down-signal", window_accel,
	                           GDK_Down, GDK_SHIFT_MASK, 0);

	/* Connect key signals */
	g_signal_connect(G_OBJECT(combo), "key-signal",
	                 G_CALLBACK(combo_open), NULL);
	g_signal_connect(G_OBJECT(combo), "up-signal",
	                 G_CALLBACK(combo_up), NULL);
	g_signal_connect(G_OBJECT(combo), "down-signal",
	                 G_CALLBACK(combo_down), GINT_TO_POINTER(4));

	/* Show everything */
	gtk_widget_show_all(combo);

	/* Redraw everything */
	redraw_everything();

	/* Process events */
	gtk_main();

	/* Get selection */
	i = gtk_combo_box_get_active(GTK_COMBO_BOX(combo));

	/* Destroy combo box */
	gtk_widget_destroy(combo);

	/* Return choice */
	return i + 2;
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
 * Updates the sensitivity on menu items.
 */
void update_menu_items(void)
{
	/* Only control sensitivity when disconnected */
	if (client_state != CS_DISCONN) return;

	/* Check for no undo possibility */
	if (num_undo == 0)
	{
		/* Disable undo items */
		gtk_widget_set_sensitive(undo_item, FALSE);
		gtk_widget_set_sensitive(undo_round_item, FALSE);
		gtk_widget_set_sensitive(undo_game_item, FALSE);
	}
	else
	{
		/* Enable undo items */
		gtk_widget_set_sensitive(undo_item, TRUE);
		gtk_widget_set_sensitive(undo_round_item, TRUE);
		gtk_widget_set_sensitive(undo_game_item, TRUE);
	}

	/* Check for no redo possibility */
	if (num_undo == max_undo)
	{
		/* Disable redo items */
		gtk_widget_set_sensitive(redo_item, FALSE);
		gtk_widget_set_sensitive(redo_round_item, FALSE);
		gtk_widget_set_sensitive(redo_game_item, FALSE);
	}
	else
	{
		/* Enable redo items */
		gtk_widget_set_sensitive(redo_item, TRUE);
		gtk_widget_set_sensitive(redo_round_item, TRUE);
		gtk_widget_set_sensitive(redo_game_item, TRUE);
	}

	/* Enable save item */
	gtk_widget_set_sensitive(save_item, TRUE);
}

/*
 * Auto save during the game.
 */
static void auto_save_choice(game *g, int who)
{
	char *full_name;

	/* Check for autosave disabled */
	if (client_state != CS_DISCONN || !opt.auto_save) return;

	/* Build full file name */
	full_name = g_build_filename(opt.data_folder ? opt.data_folder : RFTGDIR,
	                             "autosave.rftg", NULL);

	/* Save to file */
	if (save_game(g, full_name, who) < 0)
	{
		/* Error */
	}
	else
	{
		/* Set the tampered saved flag */
		game_tampered |= TAMPERED_SAVE;
	}

	/* Destroy filename */
	g_free(full_name);
}

/*
 * Auto save at the end of the game.
 */
static void auto_save_end(game *g, int who)
{
	char *full_name;

	/* Check for autosave disabled */
	if (client_state != CS_DISCONN || !opt.auto_save) return;

	/* Build file name of choice save file */
	full_name = g_build_filename(opt.data_folder ? opt.data_folder : RFTGDIR,
	                             "autosave.rftg", NULL);

	/* Delete the choice save file */
	unlink(full_name);

	/* Destroy filename */
	g_free(full_name);

	/* Build file name of end auto save file */
	full_name = g_build_filename(opt.data_folder ? opt.data_folder : RFTGDIR,
	                             "autosave_end.rftg", NULL);

	/* Save to file */
	if (save_game(g, full_name, who) < 0)
	{
		/* Error */
	}
	else
	{
		/* Set the tampered saved flag */
		game_tampered |= TAMPERED_SAVE;
	}

	/* Destroy filename */
	g_free(full_name);
}

/*
 * Load an auto save file.
 */
static int load_auto_save(game *g)
{
	char *full_name;
	int i;

	/* Build full file name */
	full_name = g_build_filename(opt.data_folder ? opt.data_folder : RFTGDIR,
	                             "autosave.rftg", NULL);

	/* Loop over players */
	for (i = 0; i < MAX_PLAYER; i++)
	{
		/* Set choice log pointer */
		g->p[i].choice_log = orig_log[i];
	}

	/* Try to load savefile into game */
	if (load_game(g, full_name) < 0)
	{
		/* Destroy filename */
		g_free(full_name);

		/* Give up */
		return FALSE;
	}

	/* Destroy filename */
	g_free(full_name);

	/* Loop over players */
	for (i = 0; i < g->num_players; ++i)
	{
		/* Remember log size */
		orig_log_size[i] = g->p[i].choice_size;
	}

	/* Force current game over */
	g->game_over = 1;

	/* Game successfully loaded */
	return TRUE;
}

/*
 * Should be called when a choice is done, in order to update undo information.
 */
static void choice_done(game *g)
{
	int i;

	/* Loop over all players */
	for (i = 0; i < g->num_players; ++i)
	{
		/* Skip human player */
		if (i != player_us)
		{
			/* Reset size of log */
			g->p[i].choice_size = g->p[i].choice_unread_pos;
		}

		/* Remember new log size */
		orig_log_size[((i-player_us) + g->num_players) % g->num_players] =
			g->p[i].choice_size;
	}

	/* Stop game replaying */
	game_replaying = FALSE;

	/* Add one to undo position */
	++num_undo;

	/* Clear redo possibility */
	max_undo = num_undo;
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

	/* Auto save */
	auto_save_choice(g, who);

	/* Update menu items */
	update_menu_items();

	/* Reset accel keys */
	accel_used = FALSE;

	/* Determine type of choice */
	switch (type)
	{
		/* Action(s) to play */
		case CHOICE_ACTION:

			/* Choose actions */
			gui_choose_action(g, who, list, arg1);

			/* Save Psi-Crystal info for redo/undo */
			rv = arg1;
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
			               arg2, arg3);
			rv = 0;
			break;

		/* Choose a settle power to use */
		case CHOICE_SETTLE:

			/* Choose power */
			gui_choose_settle(g, who, list, special, nl, ns);
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
			gui_choose_takeover_prevent(g, who, list, nl,
			                            special, ns);
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
			gui_choose_consume(g, who, list, special, nl, ns, arg1);
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

		/* Choose color of Alien Oort Cloud Refinery */
		case CHOICE_OORT_KIND:

			/* Choose type */
			rv = gui_choose_oort_kind(g, who);
			break;

		/* Error */
		default:
			display_error("Unknown choice type!\n");
			exit(1);
	}

	/* Check for aborted game */
	if (g->game_over) return;

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

	/* Mark one choice is done */
	choice_done(g);
}

/*
 * Interface to GUI decision functions.
 */
decisions gui_func =
{
	NULL,
	gui_notify_rotation,
	NULL,
	gui_make_choice,
	NULL,
	NULL,
	NULL,
	NULL,
	message_add_private,
};

/*
 * Apply options to game structure.
 */
static void apply_options(void)
{
	int i;

	/* Sanity check number of players in base game */
	if (opt.expanded < 1 && opt.num_players > 4)
	{
		/* Reset to four players */
		opt.num_players = 4;
	}

	/* Sanity check number of players in first or fourth expansion */
	if ((opt.expanded < 2 || opt.expanded == 4) && opt.num_players > 5)
	{
		/* Reset to five players */
		opt.num_players = 5;
	}

	/* Set name of human player */
	real_game.human_name = opt.player_name;

	/* Set number of players */
	real_game.num_players = opt.num_players;

	/* Set expansion level */
	real_game.expanded = opt.expanded;

	/* Set advanced flag */
	real_game.advanced = opt.advanced;

#if 0
	/* Set promo flag */
	real_game.promo = opt.promo;
#endif
	real_game.promo = 0;

	/* Set goals disabled */
	real_game.goal_disabled = opt.disable_goal;

	/* Set takeover disabled */
	real_game.takeover_disabled = opt.disable_takeover;

	/* Check for custom seed value */
	if (opt.customize_seed)
	{
		/* Set start seed */
		real_game.random_seed = opt.seed;
	}
	else
	{
		/* Set random seed */
		real_game.random_seed = time(NULL) + games_started++;
	}

	/* Sanity check advanced mode */
	if (real_game.num_players > 2)
	{
		/* Clear advanced mode */
		real_game.advanced = 0;
	}

	/* Assume no campaign */
	real_game.camp = NULL;

	/* Check for campaign name set */
	if (opt.campaign_name)
	{
		/* Loop over available campaigns */
		for (i = 0; i < num_campaign; i++)
		{
			/* Check for match */
			if (!strcmp(opt.campaign_name, camp_library[i].name))
			{
				/* Set campaign */
				real_game.camp = &camp_library[i];
			}
		}
	}

	/* Apply campaign options (number of players, etc) */
	apply_campaign(&real_game);
}

/*
 * Reset player structures.
 */
void reset_gui(void)
{
	int i;

	/* Reset our player index */
	player_us = 0;

	/* Restore opponent areas to original */
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
		/* Check for name already set for human player */
		if (i == player_us && real_game.human_name &&
		    strlen(real_game.human_name))
		{
			/* Load name */
			real_game.p[i].name = real_game.human_name;
		}
		else
		{
			/* Set name */
			real_game.p[i].name = player_names[i];
		}

		/* Restore choice log */
		real_game.p[i].choice_log = orig_log[i];
		real_game.p[i].choice_pos = 0;
		real_game.p[i].choice_unread_pos = 0;

		/* Log size already set when game is loaded or replayed */
		if (restart_loop != RESTART_LOAD && restart_loop != RESTART_REPLAY)
		{
			/* Set size of player's logs */
			real_game.p[i].choice_size = orig_log_size[i];
		}
	}

	/* Restore player control functions */
	real_game.p[player_us].control = &gui_func;
	real_game.p[player_us].ai = FALSE;

	/* Loop over AI players */
	for (i = 1; i < MAX_PLAYER; i++)
	{
		/* Set control to AI functions */
		real_game.p[i].control = &ai_func;
		real_game.p[i].ai = TRUE;

		/* Call initialization function */
		real_game.p[i].control->init(&real_game, i, 0.0);
	}

	/* Clear message log */
	clear_log();
}

/*
 * Modify GUI elements for the correct number of players.
 */
void modify_gui(int reset_card)
{
	int i;

	/* Check for goals disabled */
	if (!goals_enabled(&real_game))
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
	if (opt.hide_card == 2)
	{
		/* Hide image */
		gtk_widget_hide(full_image);
	}
	else
	{
		/* Show image */
		gtk_widget_show(full_image);
	}

	/* Reset full card */
	if (reset_card) update_card(card_back);

	/* Redraw full-size image */
	redraw_full(NULL, NULL, NULL);

	/* Resize log window */
	gtk_widget_set_size_request(message_view,
	    (opt.log_width ? opt.log_width : CARD_WIDTH) - 20, -1);

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
	int i;
	int pos, choice, saved_choice;

	/* Loop forever */
	while (1)
	{
		/* Replay by default */
		game_replaying = TRUE;

		/* Check for new game starting */
		if (restart_loop == RESTART_NEW)
		{
			/* Read parameters from options */
			apply_options();

			/* Reset our position and GUI elements */
			reset_gui();

			/* Initialize game */
			init_game(&real_game);

			/* Reset tampered state */
			game_tampered = opt.customize_seed ? TAMPERED_SEED : 0;

			/* Do not force seed for next game */
			opt.customize_seed = FALSE;

			/* Reset undo positions */
			num_undo = 0;
			max_undo = 0;

			/* Loop over players */
			for (i = 0; i < real_game.num_players; i++)
			{
				/* Clear choice log */
				real_game.p[i].choice_size = 0;
				orig_log_size[i] = 0;
			}

			/* Unset replaying flag */
			game_replaying = FALSE;

			/* Modify GUI for new game parameters */
			modify_gui(TRUE);
		}

		/* Check for restoring game */
		else if (restart_loop == RESTART_RESTORE)
		{
			/* Check for auto save enabled and auto save present */
			if (opt.auto_save && load_auto_save(&real_game))
			{
				/* Restart loaded game */
				restart_loop = RESTART_LOAD;
			}
			else
			{
				/* Just start a new game */
				restart_loop = RESTART_NEW;
			}

			/* Restart main loop */
			continue;
		}

		/* Holding pattern for multiplayer */
		else if (restart_loop == RESTART_NONE)
		{
			/* Do nothing until disconnected from server */
			while (restart_loop == RESTART_NONE)
			{
				/* Wait for events */
				gtk_main();
			}

			/* Restore single-player game */
			restart_loop = RESTART_RESTORE;
			continue;
		}

		/* Undo previous choice */
		else if (restart_loop == RESTART_UNDO)
		{
			/* Reset our position and GUI elements */
			reset_gui();

			/* Start with start of game random seed */
			real_game.random_seed = real_game.start_seed;

			/* Initialize game */
			init_game(&real_game);

			/* Remove one state from undo list */
			if (num_undo > 0) num_undo--;
		}

		/* Undo current round */
		else if (restart_loop == RESTART_UNDO_ROUND)
		{
			/* Reset our position and GUI elements */
			reset_gui();

			/* Start with start of game random seed */
			real_game.random_seed = real_game.start_seed;

			/* Initialize game */
			init_game(&real_game);

			/* Reset counts */
			pos = choice = saved_choice = 0;

			/* Count to num_undo choices */
			while (choice < num_undo && pos < real_game.p[0].choice_size)
			{
				/* Check if the current position is a round boundary */
				if (is_round_boundary(real_game.advanced,
				                      real_game.p[0].choice_log + pos))
				{
					/* Save the current choice */
					saved_choice = choice;
				}

				/* Update the position */
				pos = next_choice(real_game.p[0].choice_log, pos);

				/* Add one to choice count */
				++choice;
			}

			/* Set the undo position at the previous round boundary */
			num_undo = saved_choice;
		}

		/* Undo game */
		else if (restart_loop == RESTART_UNDO_GAME)
		{
			/* Reset our position and GUI elements */
			reset_gui();

			/* Start with start of game random seed */
			real_game.random_seed = real_game.start_seed;

			/* Initialize game */
			init_game(&real_game);

			/* Start from the beginning */
			num_undo = 0;
		}

		/* Redo current choice */
		else if (restart_loop == RESTART_REDO)
		{
			/* Reset our position and GUI elements */
			reset_gui();

			/* Start with start of game random seed */
			real_game.random_seed = real_game.start_seed;

			/* Initialize game */
			init_game(&real_game);

			/* Add one to undo position */
			++num_undo;
		}

		/* Redo current round */
		else if (restart_loop == RESTART_REDO_ROUND)
		{
			/* Reset our position and GUI elements */
			reset_gui();

			/* Start with start of game random seed */
			real_game.random_seed = real_game.start_seed;

			/* Initialize game */
			init_game(&real_game);

			/* Reset counts */
			pos = choice = 0;
			saved_choice = -1;

			/* Count to num_undo choices */
			while (choice <= num_undo && pos < real_game.p[0].choice_size)
			{
				/* Update position */
				pos = next_choice(real_game.p[0].choice_log, pos);

				/* Add one to choice count */
				++choice;
			}

			/* Loop until end of log */
			while (pos < real_game.p[0].choice_size)
			{
				/* Check for round boundary */
				if (is_round_boundary(real_game.advanced,
				                      real_game.p[0].choice_log + pos))
				{
					/* Save the current choice */
					saved_choice = choice;
					break;
				}

				/* Update position */
				pos = next_choice(real_game.p[0].choice_log, pos);

				/* Add one to choice count */
				++choice;
			}

			/* Check if choice was found */
			if (saved_choice >= 0)
			{
				/* Set the undo position at the next round boundary */
				num_undo = saved_choice;
			}
			else
			{
				/* Set the undo position at the end of the log */
				num_undo = choice;
			}
		}

		/* Redo to end of current game */
		else if (restart_loop == RESTART_REDO_GAME)
		{
			/* Reset our position and GUI elements */
			reset_gui();

			/* Start with start of game random seed */
			real_game.random_seed = real_game.start_seed;

			/* Initialize game */
			init_game(&real_game);

			/* Set undo point (will be reduced later) */
			num_undo = 9999;
		}

		/* Load a new game */
		else if (restart_loop == RESTART_LOAD)
		{
			/* Reset our position and GUI elements */
			reset_gui();

			/* Set tampered loaded flag */
			game_tampered = TAMPERED_LOAD;

			/* Start with start of game random seed */
			real_game.random_seed = real_game.start_seed;

			/* Initialize game */
			init_game(&real_game);

			/* Set undo point (will be reduced later) */
			num_undo = 9999;

			/* Modify GUI for new game parameters */
			modify_gui(TRUE);
		}

		/* Replay a loaded game */
		else if (restart_loop == RESTART_REPLAY)
		{
			/* Reset our position and GUI elements */
			reset_gui();

			/* Set tampered loaded flag */
			game_tampered = TAMPERED_LOAD;

			/* Start with start of game random seed */
			real_game.random_seed = real_game.start_seed;

			/* Initialize game */
			init_game(&real_game);

			/* Begin at start */
			num_undo = 0;

			/* Modify GUI for new game parameters */
			modify_gui(TRUE);
		}

		/* Replay to current position (to regenerate log) */
		else if (restart_loop == RESTART_CURRENT)
		{
			/* Reset our position and GUI elements */
			reset_gui();

			/* Start with start of game random seed */
			real_game.random_seed = real_game.start_seed;

			/* Initialize game */
			init_game(&real_game);
		}

		/* Reset counts */
		pos = choice = 0;

		/* Count to num_undo choices */
		while (choice < num_undo && pos < real_game.p[0].choice_size)
		{
			/* Update log position */
			pos = next_choice(real_game.p[0].choice_log, pos);

			/* Add one to choice count */
			++choice;
		}

		/* Set the current undo point (in case the log was too small) */
		num_undo = choice;

		/* Reset the size choice of the human player */
		real_game.p[0].choice_size = pos;

		/* Find total number of replay points */
		while (pos < orig_log_size[0])
		{
			/* Update log position */
			pos = next_choice(real_game.p[0].choice_log, pos);

			/* Add one to choice count */
			++choice;
		}

		/* Set the max number of undo positions in the log */
		max_undo = choice;

		/* Clear restart loop flag */
		restart_loop = 0;

		/* Begin game */
		begin_game(&real_game);

		/* Check for aborted game */
		if (real_game.game_over) continue;

		/* Play game rounds until finished */
		while (game_round(&real_game));

		/* Check for restart request */
		if (restart_loop)
		{
			/* Restart loop */
			continue;
		}

		/* Deactivate action button */
		gtk_widget_set_sensitive(action_button, FALSE);

		/* Declare winner */
		declare_winner(&real_game);

		/* Format seed message */
		sprintf(buf, "(The seed for this game was %u.)\n", real_game.start_seed);

		/* Send message */
		message_add(&real_game, buf);

		/* Check for tampered game */
		if (game_tampered & TAMPERED_MOVE)
		{
			/* Add tampered note */
			message_add(&real_game, "(Debug game.)\n");
		}

		/* Auto save */
		auto_save_end(&real_game, player_us);

		/* Check if game is replaying */
		if (!game_replaying)
		{
			/* Auto export game */
			auto_export();
		}

		/* Reset displayed cards */
		reset_cards(&real_game, TRUE, TRUE);

		/* Redraw everything */
		redraw_everything();

		/* Create prompt */
		sprintf(buf, "Game Over");

		/* Set prompt */
		gtk_label_set_text(GTK_LABEL(action_prompt), buf);

		/* Update menu items */
		update_menu_items();

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
	opt.player_name = g_key_file_get_string(pref_file, "game",
	                                       "name", NULL);
	opt.expanded = g_key_file_get_integer(pref_file, "game",
	                                      "expansion", NULL);
	opt.advanced = g_key_file_get_boolean(pref_file, "game",
	                                      "advanced", NULL);
	opt.promo = g_key_file_get_boolean(pref_file, "game", "promo", NULL);
	opt.disable_goal = g_key_file_get_boolean(pref_file, "game",
	                                          "no_goals", NULL);
	opt.disable_takeover = g_key_file_get_boolean(pref_file, "game",
	                                              "no_takeover", NULL);
	/* Read campaign options */
	opt.campaign_name = g_key_file_get_string(pref_file, "game", "campaign",
	                                          NULL);

	/* Check length of human name */
	if (opt.player_name && strlen(opt.player_name) > 50)
	{
		/* Cap length of name */
		opt.player_name[50] = '\0';
	}

	/* Read GUI options */
	opt.hide_card = g_key_file_get_integer(pref_file, "gui",
	                                       "full_reduced", NULL);
	opt.card_size = g_key_file_get_integer(pref_file, "gui",
	                                       "card_size", NULL);
	opt.log_width = g_key_file_get_integer(pref_file, "gui",
	                                       "log_width", NULL);
	opt.shrink_opponent = g_key_file_get_boolean(pref_file, "gui",
	                                             "shrink_opponent", NULL);
	opt.settle_discount = g_key_file_get_boolean(pref_file, "gui",
	                                             "settle_discount", NULL);
	opt.vp_in_hand = g_key_file_get_boolean(pref_file, "gui",
	                                        "vp_in_hand", NULL);
	opt.cost_in_hand = g_key_file_get_boolean(pref_file, "gui",
	                                          "cost_in_hand", NULL);
	opt.key_cues = g_key_file_get_boolean(pref_file, "gui",
	                                      "key_cues", NULL);
	opt.auto_select = g_key_file_get_boolean(pref_file, "gui",
	                                         "auto_select", NULL);
	opt.auto_save = g_key_file_get_boolean(pref_file, "gui",
	                                       "auto_save", NULL);

	/* Check for auto_export key present (since 0.8.1l) */
	if (g_key_file_has_key(pref_file, "gui", "auto_export", NULL))
	{
		opt.auto_export = g_key_file_get_boolean(pref_file, "gui",
		                                         "auto_export", NULL);
	}
	else
	{
		/* For backwards compatibility, read the old save_log key */
		opt.auto_export = g_key_file_get_boolean(pref_file, "gui",
		                                         "save_log", NULL);
	}

	opt.colored_log = g_key_file_get_boolean(pref_file, "gui",
	                                         "colored_log", NULL);
	opt.verbose_log = g_key_file_get_boolean(pref_file, "gui",
	                                        "verbose_log", NULL);
	opt.draw_log = g_key_file_get_boolean(pref_file, "gui",
	                                      "draw_log", NULL);
	opt.discard_log = g_key_file_get_boolean(pref_file, "gui",
	                                         "discard_log", NULL);

	/* Read folder options */
	opt.last_save = g_key_file_get_string(pref_file, "folders",
	                                      "last_save", NULL);
	opt.data_folder = g_key_file_get_string(pref_file, "folders",
	                                       "data_folder", NULL);
	opt.export_folder = g_key_file_get_string(pref_file, "folders",
	                                          "export_folder", NULL);

	/* Read multiplayer options */
	opt.server_name = g_key_file_get_string(pref_file, "multiplayer",
	                                        "server_name", NULL);
	opt.server_port = g_key_file_get_integer(pref_file, "multiplayer",
	                                         "server_port", NULL);
	opt.username = g_key_file_get_string(pref_file, "multiplayer",
	                                     "username", NULL);
	opt.password = g_key_file_get_string(pref_file, "multiplayer",
	                                     "password", NULL);

	/* Read multiplayer game creation options */
	opt.game_desc = g_key_file_get_string(pref_file, "multiplayer",
	                                      "game_desc", NULL);
	opt.game_pass = g_key_file_get_string(pref_file, "multiplayer",
	                                      "game_pass", NULL);
	opt.multi_min = g_key_file_get_integer(pref_file, "multiplayer",
	                                       "min_player", NULL);
	opt.multi_max = g_key_file_get_integer(pref_file, "multiplayer",
	                                       "max_player", NULL);

	/* Read export options */
	opt.export_style_sheet = g_key_file_get_string(pref_file, "export",
	                                               "style_sheet", NULL);

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
	char msg[1024];

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
	g_key_file_set_string(pref_file, "game", "name", opt.player_name);
	g_key_file_set_integer(pref_file, "game", "expansion", opt.expanded);
	g_key_file_set_boolean(pref_file, "game", "advanced", opt.advanced);
	g_key_file_set_boolean(pref_file, "game", "promo", opt.promo);
	g_key_file_set_boolean(pref_file, "game", "no_goals", opt.disable_goal);
	g_key_file_set_boolean(pref_file, "game", "no_takeover",
	                       opt.disable_takeover);

	/* Set campaign (if any) */
	g_key_file_set_string(pref_file, "game", "campaign", opt.campaign_name);

	/* Set GUI options */
	g_key_file_set_integer(pref_file, "gui", "full_reduced",
	                       opt.hide_card);
	g_key_file_set_integer(pref_file, "gui", "card_size",
	                       opt.card_size);
	g_key_file_set_integer(pref_file, "gui", "log_width",
	                       opt.log_width);
	g_key_file_set_boolean(pref_file, "gui", "shrink_opponent",
	                       opt.shrink_opponent);
	g_key_file_set_boolean(pref_file, "gui", "settle_discount",
	                       opt.settle_discount);
	g_key_file_set_boolean(pref_file, "gui", "vp_in_hand",
	                       opt.vp_in_hand);
	g_key_file_set_boolean(pref_file, "gui", "cost_in_hand",
	                       opt.cost_in_hand);
	g_key_file_set_boolean(pref_file, "gui", "key_cues",
	                       opt.key_cues);
	g_key_file_set_boolean(pref_file, "gui", "auto_select",
	                       opt.auto_select);
	g_key_file_set_boolean(pref_file, "gui", "auto_save",
		                   opt.auto_save);
	g_key_file_set_boolean(pref_file, "gui", "auto_export",
		                   opt.auto_export);
	g_key_file_set_boolean(pref_file, "gui", "colored_log",
		                   opt.colored_log);
	g_key_file_set_boolean(pref_file, "gui", "verbose_log",
		                   opt.verbose_log);
	g_key_file_set_boolean(pref_file, "gui", "draw_log",
		                   opt.draw_log);
	g_key_file_set_boolean(pref_file, "gui", "discard_log",
		                   opt.discard_log);

	/* Set folder location options */
	g_key_file_set_string(pref_file, "folders", "last_save",
	                      opt.last_save);
	g_key_file_set_string(pref_file, "folders", "data_folder",
	                      opt.data_folder);
	g_key_file_set_string(pref_file, "folders", "export_folder",
	                      opt.export_folder);

	/* Set multiplayer options */
	g_key_file_set_string(pref_file, "multiplayer", "server_name",
	                      opt.server_name);
	g_key_file_set_integer(pref_file, "multiplayer", "server_port",
	                       opt.server_port);
	g_key_file_set_string(pref_file, "multiplayer", "username",
	                      opt.username);
	g_key_file_set_string(pref_file, "multiplayer", "password",
	                      opt.password);

	/* Set multiplayer game creation options */
	g_key_file_set_string(pref_file, "multiplayer", "game_desc",
	                      opt.game_desc);
	g_key_file_set_string(pref_file, "multiplayer", "game_pass",
	                      opt.game_pass);
	g_key_file_set_integer(pref_file, "multiplayer", "min_player",
	                       opt.multi_min);
	g_key_file_set_integer(pref_file, "multiplayer", "max_player",
	                       opt.multi_max);

	/* Set export options */
	g_key_file_set_string(pref_file, "export", "style_sheet",
	                      opt.export_style_sheet);

	/* Open file for writing */
	fff = fopen(path, "w");

	/* Check for failure */
	if (!fff)
	{
		/* Error */
		sprintf(msg, "Warning: Can't save preferences to %s!\n", path);
		display_error(msg);
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
 * Set sensitivity of menu items based on client state.
 */
void gui_client_state_changed(int playing_game, int making_choice)
{
	/* Check if client is disconnected */
	if (client_state == CS_DISCONN)
	{
		/* Activate local game menu items */
		gtk_widget_set_sensitive(new_item, TRUE);
		gtk_widget_set_sensitive(new_parameters_item, TRUE);
		gtk_widget_set_sensitive(load_item, TRUE);
		gtk_widget_set_sensitive(replay_item, TRUE);
		gtk_widget_set_sensitive(save_item, TRUE);
		gtk_widget_set_sensitive(export_item, TRUE);
		gtk_widget_set_sensitive(option_item, TRUE);
		gtk_widget_set_sensitive(advanced_item, TRUE);
		gtk_widget_set_sensitive(undo_item, TRUE);
		gtk_widget_set_sensitive(undo_round_item, TRUE);
		gtk_widget_set_sensitive(undo_game_item, TRUE);
		gtk_widget_set_sensitive(redo_item, TRUE);
		gtk_widget_set_sensitive(redo_round_item, TRUE);
		gtk_widget_set_sensitive(redo_game_item, TRUE);
		gtk_widget_set_sensitive(debug_card_item, TRUE);
		gtk_widget_set_sensitive(debug_ai_item, TRUE);
		gtk_widget_set_sensitive(connect_item, TRUE);
		gtk_widget_set_sensitive(about_item, TRUE);

		/* Deactivate disconnect and resign menu item */
		gtk_widget_set_sensitive(disconnect_item, FALSE);
		gtk_widget_set_sensitive(resign_item, FALSE);
	}
	else
	{
		/* Deactivate local game menu items */
		gtk_widget_set_sensitive(new_item, FALSE);
		gtk_widget_set_sensitive(new_parameters_item, FALSE);
		gtk_widget_set_sensitive(load_item, FALSE);
		gtk_widget_set_sensitive(replay_item, FALSE);
		gtk_widget_set_sensitive(save_item, FALSE);
		gtk_widget_set_sensitive(undo_item, FALSE);
		gtk_widget_set_sensitive(undo_round_item, FALSE);
		gtk_widget_set_sensitive(undo_game_item, FALSE);
		gtk_widget_set_sensitive(redo_item, FALSE);
		gtk_widget_set_sensitive(redo_round_item, FALSE);
		gtk_widget_set_sensitive(redo_game_item, FALSE);
		gtk_widget_set_sensitive(debug_ai_item, FALSE);
		gtk_widget_set_sensitive(connect_item, FALSE);

		/* Activate disconnect menu item */
		gtk_widget_set_sensitive(disconnect_item, TRUE);

		/* Check if client is playing a game */
		if (playing_game)
		{
			/* Activate the resign menu item */
			gtk_widget_set_sensitive(resign_item, TRUE);

			/* Check if client is making a choice */
			/* XXX Suppressing a bug where a dialog becomes unresponsive */
			/* when receiving MSG_CHOOSE from server. This bug affects */
			/* resign_item too, but this is deliberately left active. */
			if (making_choice)
			{
				/* Activate items */
				gtk_widget_set_sensitive(export_item, TRUE);
				gtk_widget_set_sensitive(option_item, TRUE);
				gtk_widget_set_sensitive(advanced_item, TRUE);
				gtk_widget_set_sensitive(debug_card_item, debug_server);
				gtk_widget_set_sensitive(about_item, TRUE);
			}
			else
			{
				/* Deactivate items */
				gtk_widget_set_sensitive(export_item, FALSE);
				gtk_widget_set_sensitive(option_item, FALSE);
				gtk_widget_set_sensitive(advanced_item, FALSE);
				gtk_widget_set_sensitive(debug_card_item, FALSE);
				gtk_widget_set_sensitive(about_item, FALSE);
			}
		}
		else
		{
			/* Activate the options and about items */
			gtk_widget_set_sensitive(option_item, TRUE);
			gtk_widget_set_sensitive(advanced_item, TRUE);
			gtk_widget_set_sensitive(about_item, TRUE);

			/* Deactivate the resign and debug menu items */
			gtk_widget_set_sensitive(resign_item, FALSE);
			gtk_widget_set_sensitive(debug_card_item, FALSE);

			/* Set the export item depending on whether game is over or not */
			gtk_widget_set_sensitive(export_item, making_choice);
		}
	}
}

/*
 * Export log. Implemented as a callback to avoid loadsave.c depending on gtk.
 */
static void export_log(FILE *fff, int gid)
{
	GtkTextIter iter_start, iter_end;
	GtkTextBuffer *message_buffer;
	GSList *list;
	char *tag, *line;

	/* Get message buffer */
	message_buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(message_view));

	/* Get start of buffer */
	gtk_text_buffer_get_start_iter(message_buffer, &iter_start);

	/* Loop until end of buffer */
	while (!gtk_text_iter_is_end(&iter_start))
	{
		/* Find tags */
		list = gtk_text_iter_get_tags(&iter_start);

		/* XXX Only look for first tag */
		if (list)
		{
			/* Get name of tag */
			g_object_get(G_OBJECT(list->data), "name", &tag, NULL);

			/* Write xml start tag with format attribute */
			fprintf(fff, "    <Message format=\"%s\">", tag);

			/* Clean up */
			g_free(tag);
			g_slist_free(list);
		}
		else
		{
			/* Write xml start tag */
			fputs("    <Message>", fff);
		}

		/* Get end of line */
		iter_end = iter_start;
		gtk_text_iter_forward_line(&iter_end);
		gtk_text_iter_backward_char(&iter_end);

		/* Get line contents */
		line = gtk_text_iter_get_text(&iter_start, &iter_end);

		/* Write message and xml end tag */
		fprintf(fff, "%s</Message>\n", xml_escape(line));

		/* Destroy line */
		g_free(line);

		/* Get start of next line */
		iter_start = iter_end;
		gtk_text_iter_forward_char(&iter_start);
	}
}

/*
 * Callback during export to add tampered state and save game.
 */
static void export_callback(FILE *fff, int gid)
{
	/* Check for tampered game */
	if (game_tampered >= 0)
		fprintf(fff, "  <Tampered>%d</Tampered>\n", game_tampered);

	/* Check for connected client */
	if (client_state == CS_DISCONN)
	{
		/* Start game tag and CDATA */
		fputs("  <Save>\n<![CDATA[\n", fff);

		/* Write save game */
		write_game(&real_game, fff, player_us);

		/* Writed end tags */
		fputs("]]>\n  </Save>\n", fff);
	}
}

/*
 * Helper method to collect parameters to export_game.
 */
static void do_export(char* filename, const char* message)
{
	char msg[1024], server[1024];

	/* Check for connected client */
	if (client_state != CS_DISCONN)
	{
		/* Check for known server version */
		if (strlen(server_version))
		{
			/* Use server version with server name */
			sprintf(server, "%s (%s)", opt.server_name, server_version);
		}
		else
		{
			/* Use only server name */
			strcpy(server, opt.server_name);
		}
	}
	else
	{
		/* Use local name */
		strcpy(server, "local");
	}

	/* Save to file */
	if (export_game(&real_game, filename, opt.export_style_sheet, server,
	                player_us, message, num_special_cards, special_cards,
	                export_log, export_callback, 0) < 0)
	{
		/* Format error */
		sprintf(msg, "Error: Could not export game to %s!\n", filename);

		/* Display error */
		display_error(msg);
	}
}

/*
 * Exports the game to a time stamped file.
 */
void auto_export(void)
{
	FILE *fff;
	char filename[30];
	char *full_filename;

	time_t raw_time;
	struct tm* timeinfo;

	/* Check for auto export option */
	if (!opt.auto_export) return;

	/* Get the time */
	time(&raw_time);

	/* Get the local time */
	timeinfo = localtime(&raw_time);

	/* Generate file name */
	strftime(filename, 30, "export_%y%m%d_%H%M.xml", timeinfo);

	/* Build full file name */
	full_filename = g_build_filename(
	    opt.export_folder ? opt.export_folder : RFTGDIR, filename, NULL);

	/* Open file for writing */
	fff = fopen(full_filename, "w");

	/* Check for failure */
	if (!fff)
	{
		return;
	}

	/* Save to file */
	do_export(full_filename, gtk_label_get_text(GTK_LABEL(action_prompt)));

	/* Destroy filename */
	g_free(full_filename);

	/* Close file */
	fclose(fff);
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
	game load_state;
	GtkWidget *dialog;
	char *fname;
	char *header = GPOINTER_TO_INT(data) == RESTART_LOAD ?
	               "Load game" : "Replay game";
	int i;

	/* Check for connected to server */
	if (client_state != CS_DISCONN) return;

	/* Create file chooser dialog box */
	dialog = gtk_file_chooser_dialog_new(header, NULL,
	                                     GTK_FILE_CHOOSER_ACTION_OPEN,
	                                     GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
	                                     GTK_STOCK_OPEN, GTK_RESPONSE_ACCEPT,
	                                     NULL);

	/* Check if last save location is set */
	if (opt.last_save)
	{
		/* Set current folder to last save */
		gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(dialog), opt.last_save);
	}

	/* Run dialog and check response */
	if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT)
	{
		/* Get folder used */
		opt.last_save = gtk_file_chooser_get_current_folder(GTK_FILE_CHOOSER(dialog));

		/* Save prefs */
		save_prefs();

		/* Get filename */
		fname = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));

		/* Loop over players */
		for (i = 0; i < MAX_PLAYER; i++)
		{
			/* Set choice log pointer */
			load_state.p[i].choice_log = orig_log[i];
		}

		/* Clear campaign structure */
		load_state.camp_status = NULL;

		/* Try to load savefile into load state */
		if (load_game(&load_state, fname) < 0)
		{
			/* Error */
			display_error("Warning: Failed to load game!\n");

			/* Destroy filename */
			g_free(fname);

			/* Destroy load dialog */
			gtk_widget_destroy(dialog);

			/* Give up */
			return;
		}

		/* Destroy filename */
		g_free(fname);

		/* Reset GUI */
		reset_gui();

		/* Loop over players */
		for (i = 0; i < load_state.num_players; ++i)
		{
			/* Remember log sizes */
			orig_log_size[i] = load_state.p[i].choice_size;
		}

		/* Copy loaded state to real */
		real_game = load_state;

		/* Force current game over */
		real_game.game_over = 1;

		/* Switch to loaded or replay state when able */
		restart_loop = GPOINTER_TO_INT(data);

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

	/* Check if last save location is set */
	if (opt.last_save)
	{
		/* Set current folder to last save */
		gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(dialog), opt.last_save);
	}

	/* Run dialog and check response */
	if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT)
	{
		/* Get folder used */
		opt.last_save = gtk_file_chooser_get_current_folder(GTK_FILE_CHOOSER(dialog));

		/* Save prefs */
		save_prefs();

		/* Get filename */
		fname = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));

		/* Save to file */
		if (save_game(&real_game, fname, player_us) < 0)
		{
			/* Error */
		}
		else
		{
			/* Set the tampered saved flag */
			game_tampered |= TAMPERED_SAVE;
		}

		/* Destroy filename */
		g_free(fname);
	}

	/* Destroy file chooser dialog */
	gtk_widget_destroy(dialog);
}

/*
 * Export game.
 */
static void gui_export_game(GtkMenuItem *menu_item, gpointer data)
{
	GtkWidget *dialog;
	char *file_name;

	/* Create file chooser dialog box */
	dialog = gtk_file_chooser_dialog_new("Export game", NULL,
	                                     GTK_FILE_CHOOSER_ACTION_SAVE,
	                                     GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
	                                     "Export", GTK_RESPONSE_ACCEPT,
	                                     NULL);

	/* Check if last save location is set */
	if (opt.last_save)
	{
		/* Set current folder to last save */
		gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(dialog), opt.last_save);
	}

	/* Run dialog and check response */
	if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT)
	{
		/* Get folder used */
		opt.last_save =
			gtk_file_chooser_get_current_folder(GTK_FILE_CHOOSER(dialog));

		/* Save prefs */
		save_prefs();

		/* Get filename */
		file_name = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));

		/* Export to file */
		do_export(file_name, gtk_label_get_text(GTK_LABEL(action_prompt)));

		/* Destroy filename */
		g_free(file_name);
	}

	/* Destroy file chooser dialog */
	gtk_widget_destroy(dialog);
}

/*
 * Undo.
 */
static void gui_undo(GtkMenuItem *menu_item, gpointer data)
{
	/* Check for connected to server */
	if (client_state != CS_DISCONN) return;

	/* Check for nothing to undo */
	if (num_undo == 0) return;

	/* Set the tampered undo flag */
	game_tampered |= TAMPERED_UNDO;

	/* Force game over */
	real_game.game_over = 1;

	/* Switch to undo state when able */
	restart_loop = GPOINTER_TO_INT(data);

	/* Quit waiting for events */
	gtk_main_quit();
}

/*
 * Redo.
 */
static void gui_redo(GtkMenuItem *menu_item, gpointer data)
{
	/* Check for connected to server */
	if (client_state != CS_DISCONN) return;

	/* Check for nothing to redo */
	if (num_undo == max_undo) return;

	/* Set the tampered undo flag */
	game_tampered |= TAMPERED_UNDO;

	/* Force game over */
	real_game.game_over = 1;

	/* Switch to redo state when able */
	restart_loop = GPOINTER_TO_INT(data);

	/* Quit waiting for events */
	gtk_main_quit();
}

/*
 * Widgets for select dialog.
 */
static GtkWidget *num_players_radio[MAX_PLAYER];
static GtkWidget *advanced_check;
static GtkWidget *disable_goal_check;
static GtkWidget *disable_takeover_check;
static GtkWidget *name_entry;
static GtkWidget *custom_seed_check;
static GtkWidget *seed_entry;

/*
 * Current selections for next game options.
 */
static int next_exp, next_player;

/*
 * Update button sensitivities.
 */
static void update_sensitivity()
{
	int i;

	/* Set advanced checkbox sensitivity */
	gtk_widget_set_sensitive(advanced_check, next_player == 2);

	/* Set goal disabled checkbox sensitivity */
	gtk_widget_set_sensitive(disable_goal_check, next_exp > 0 && next_exp < 4);

	/* Set takeover disabled checkbox sensitivity */
	gtk_widget_set_sensitive(disable_takeover_check, next_exp > 1 && next_exp < 4);

	/* Set player radio sensitivities */
	for (i = 0; player_labels[i]; ++i)
	{
		gtk_widget_set_sensitive(num_players_radio[i], i < (next_exp == 4 ? 4 : next_exp + 3));
	}
}

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

		/* Update sensitivites */
		update_sensitivity();
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

		/* Update sensitivites */
		update_sensitivity();
	}
}

/*
 * React to specify custom seed button being toggled.
 */
static void seed_toggle(GtkToggleButton *button, gpointer data)
{
	/* Check whether the toggle button is active */
	if (gtk_toggle_button_get_active(button))
	{
		/* Enable the seed value entry */
		gtk_widget_set_sensitive(seed_entry, TRUE);

		/* Give focus to the seed entry */
		gtk_widget_grab_focus(seed_entry);
	}
	else
	{
		/* Disable the seed value entry */
		gtk_widget_set_sensitive(seed_entry, FALSE);
	}

}

/*
 * Callback when hide card toggle button is changed.
 */
static void hide_card_changed(GtkToggleButton *hide_card_button,
                              gpointer card_size_scale)
{
	/* Disable card size if card is hidden */
	gtk_widget_set_sensitive(GTK_WIDGET(card_size_scale),
	                         !gtk_toggle_button_get_active(hide_card_button));

	/* Update options */
	opt.hide_card = 2 * gtk_toggle_button_get_active(hide_card_button);

	/* Handle new options */
	modify_gui(FALSE);
}

/*
 * Callback when card size scale changes.
 */
static void card_size_changed(GtkRange *card_size_scale,
                              gpointer log_width_scale)
{
	double log_width;

	/* Update options */
	opt.card_size = (int) gtk_range_get_value(card_size_scale);

	/* Reset scale (adjust for rounding) */
	gtk_range_set_value(card_size_scale, opt.card_size);

	/* Get current log width */
	log_width = gtk_range_get_value(GTK_RANGE(log_width_scale));

	/* Update log width if needed */
	if (opt.card_size > log_width)
	{
		/* Update scale */
		gtk_range_set_value(GTK_RANGE(log_width_scale), opt.card_size);

		/* Update options */
		opt.log_width = opt.card_size;
	}

	/* Handle new options */
	modify_gui(FALSE);
}

/*
 * Callback when log width scale changes.
 */
static void log_width_changed(GtkRange *log_width_scale,
                              gpointer card_size_scale)
{
	double card_size;

	/* Update options */
	opt.log_width = (int) gtk_range_get_value(log_width_scale);

	/* Reset scale (adjust for rounding) */
	gtk_range_set_value(log_width_scale, opt.log_width);

	/* Get current card size */
	card_size = gtk_range_get_value(GTK_RANGE(card_size_scale));

	/* Update card size if needed */
	if (opt.log_width < card_size)
	{
		/* Update scale */
		gtk_range_set_value(GTK_RANGE(card_size_scale), opt.log_width);

		/* Update options */
		opt.card_size = opt.log_width;
	}

	/* Redraw phase bar */
	redraw_phase();

	/* Handle new options */
	modify_gui(FALSE);
}

/*
 * Callback when a GUI option button is toggled.
 */
static void update_option(GtkToggleButton *button, gpointer option)
{
	int *opt_ptr = (int *)option;

	/* Update option */
	*opt_ptr = gtk_toggle_button_get_active(button);

	/* Handle new options */
	modify_gui(FALSE);

	/* Redraw everything */
	redraw_everything();
}

/*
 * Callback to trigger the accept response of a dialog
 */
static void enter_callback(GtkWidget *widget, GtkWidget *dialog)
{
    g_signal_emit_by_name(G_OBJECT(dialog), "response", GTK_RESPONSE_ACCEPT);
}

/*
 * Select parameters and start a new game.
 */
static void gui_new_parameters(GtkMenuItem *menu_item, gpointer data)
{
	GtkWidget *dialog;
	GtkWidget *radio = NULL;
	GtkWidget *exp_box, *player_box, *name_box, *seed_box, *seed_value_box;
	GtkWidget *exp_frame, *player_frame, *seed_frame;
	GtkWidget *name_label, *seed_label;
	int i;

	/* Check for connected to server */
	if (client_state != CS_DISCONN) return;

	/* Create dialog box */
	dialog = gtk_dialog_new_with_buttons("Select Parameters", NULL,
	                                     GTK_DIALOG_MODAL,
	                                     "New Game",
	                                     GTK_RESPONSE_ACCEPT,
	                                     GTK_STOCK_CANCEL,
	                                     GTK_RESPONSE_REJECT, NULL);

	/* Set window title */
	gtk_window_set_title(GTK_WINDOW(dialog), TITLE);

	/* Create hbox to hold player name label and entry */
	name_box = gtk_hbox_new(FALSE, 0);

	/* Create name label */
	name_label = gtk_label_new("Player name:");

	/* Pack name label into name box */
	gtk_box_pack_start(GTK_BOX(name_box), name_label, FALSE, TRUE, 5);

	/* Create text entry for name */
	name_entry = gtk_entry_new();

	/* Set max length */
	gtk_entry_set_max_length(GTK_ENTRY(name_entry), 50);

	/* Set request size */
	gtk_widget_set_size_request(name_entry, 120, -1);

	/* Set name contents */
	gtk_entry_set_text(GTK_ENTRY(name_entry),
	                   opt.player_name ? opt.player_name : "");

	/* Connect the name entry's activate signal to */
	/* the accept response on the dialog */
	g_signal_connect(G_OBJECT(name_entry), "activate",
	                 G_CALLBACK(enter_callback), (gpointer) dialog);

	/* Pack name entry into name box */
	gtk_box_pack_start(GTK_BOX(name_box), name_entry, FALSE, TRUE, 0);

	/* Pack name box into dialog box */
	gtk_container_add(GTK_CONTAINER(GTK_DIALOG(dialog)->vbox), name_box);

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

	/* Loop over number of players */
	for (i = 0; player_labels[i]; i++)
	{
		/* Create radio button */
		radio = gtk_radio_button_new_with_label_from_widget(
		                                        GTK_RADIO_BUTTON(radio),
		                                        player_labels[i]);

		/* Remember radio button */
		num_players_radio[i] = radio;

		/* Check for current number of players */
		if (real_game.num_players == i + 2)
		{
			/* Set button active */
			gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(radio),
			                             TRUE);

			/* Remember current number of players */
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
	gtk_container_add(GTK_CONTAINER(GTK_DIALOG(dialog)->vbox), player_frame);

	/* Create check box for two-player advanced game */
	advanced_check = gtk_check_button_new_with_label("Two-player advanced");

	/* Set checkbox status */
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(advanced_check),
	                             opt.advanced);

	/* Add checkbox to dialog box */
	gtk_container_add(GTK_CONTAINER(GTK_DIALOG(dialog)->vbox),
	                  advanced_check);

#if 0
	/* Create check box for promo start worlds */
	promo_check = gtk_check_button_new_with_label("Include promo cards");

	/* Set checkbox status */
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(promo_check), opt.promo);

	/* Add checkbox to dialog box */
	gtk_container_add(GTK_CONTAINER(GTK_DIALOG(dialog)->vbox), promo_check);
#endif

	/* Create check box for disabled goals */
	disable_goal_check = gtk_check_button_new_with_label("Disable goals");

	/* Set checkbox status */
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(disable_goal_check),
	                             opt.disable_goal);

	/* Add checkbox to dialog box */
	gtk_container_add(GTK_CONTAINER(GTK_DIALOG(dialog)->vbox),
	                  disable_goal_check);

	/* Create check box for disabled takeovers */
	disable_takeover_check =
	                   gtk_check_button_new_with_label("Disable takeovers");

	/* Set checkbox status */
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(disable_takeover_check),
	                             opt.disable_takeover);

	/* Add checkbox to dialog box */
	gtk_container_add(GTK_CONTAINER(GTK_DIALOG(dialog)->vbox),
	                  disable_takeover_check);

	/* Create vbox to hold seed specification widgets */
	seed_box = gtk_vbox_new(FALSE, 0);

	/* Create checkbox for specifying custom seed */
	custom_seed_check = gtk_check_button_new_with_label("Specify custom seed");

	/* Add handler */
	g_signal_connect(G_OBJECT(custom_seed_check), "toggled",
	                 G_CALLBACK(seed_toggle), NULL);

	/* Pack checkbox into seed box */
	gtk_box_pack_start(GTK_BOX(seed_box), custom_seed_check, FALSE, TRUE, 0);

	/* Create hbox to hold seed label and entry */
	seed_value_box = gtk_hbox_new(FALSE, 0);

	/* Create seed label */
	seed_label = gtk_label_new("Value:");

	/* Pack seed label into seed value box */
	gtk_box_pack_start(GTK_BOX(seed_value_box), seed_label, FALSE, TRUE, 5);

	/* Create text entry for seed */
	seed_entry = gtk_entry_new();

	/* Set max length */
	gtk_entry_set_max_length(GTK_ENTRY(seed_entry), 10);

	/* Set request size */
	gtk_widget_set_size_request(seed_entry, 120, -1);

	/* Disable seed entry box */
	gtk_widget_set_sensitive(seed_entry, FALSE);

	/* Connect the seed entry's activate signal to */
	/* the accept response on the dialog */
	g_signal_connect(G_OBJECT(seed_entry), "activate",
	                 G_CALLBACK(enter_callback), (gpointer) dialog);

	/* Pack seed entry into seed value box */
	gtk_box_pack_start(GTK_BOX(seed_value_box), seed_entry, FALSE, TRUE, 0);

	/* Pack seed value box into seed box */
	gtk_box_pack_start(GTK_BOX(seed_box), seed_value_box, FALSE, TRUE, 0);

	/* Create frame around seed widgets */
	seed_frame = gtk_frame_new("Random seed");

	/* Pack seed widgets into seed frame */
	gtk_container_add(GTK_CONTAINER(seed_frame), seed_box);

	/* Add frame to dialog box */
	gtk_container_add(GTK_CONTAINER(GTK_DIALOG(dialog)->vbox), seed_frame);

	/* Update sensitivites */
	update_sensitivity();

	/* Show all widgets */
	gtk_widget_show_all(dialog);

	/* Run dialog */
	if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT)
	{
		/* Check for too many players */
		if (next_exp == 0 && next_player > 4) next_player = 4;
		if (next_exp == 1 && next_player > 5) next_player = 5;
		if (next_exp == 4 && next_player > 5) next_player = 5;

		/* Set player name */
		opt.player_name = strdup(gtk_entry_get_text(GTK_ENTRY(name_entry)));

		/* Set expansion level */
		opt.expanded = next_exp;

		/* Set number of players */
		opt.num_players = next_player;

		/* Set advanced game flag */
		opt.advanced = (next_player == 2) &&
		                gtk_toggle_button_get_active(
		                             GTK_TOGGLE_BUTTON(advanced_check));

#if 0
		/* Set promo flag */
		opt.promo = gtk_toggle_button_get_active(
						GTK_TOGGLE_BUTTON(promo_check));
#endif

		/* Set goals disabled flag */
		opt.disable_goal = (opt.expanded >= 1) && (opt.expanded <= 3) &&
		                gtk_toggle_button_get_active(
		                         GTK_TOGGLE_BUTTON(disable_goal_check));

		/* Set takeover disabled flag */
		opt.disable_takeover = (opt.expanded >= 2) && (opt.expanded <= 3) &&
		                gtk_toggle_button_get_active(
		                     GTK_TOGGLE_BUTTON(disable_takeover_check));

		/* Set custom seed flag */
		opt.customize_seed = gtk_toggle_button_get_active(
		                          GTK_TOGGLE_BUTTON(custom_seed_check));

		/* Set seed */
		opt.seed = (unsigned int) atof(gtk_entry_get_text(
		                               GTK_ENTRY(seed_entry)));

		/* Clear campaign */
		opt.campaign_name = "";

		/* Apply options */
		apply_options();

		/* Recreate GUI elements for new number of players */
		modify_gui(TRUE);

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
 * Campaign description label.
 */
static GtkWidget *campaign_desc;

/*
 * Selected campaign was changed.
 */
static void campaign_changed(GtkComboBox *combo, gpointer data)
{
	int i;

	/* Get combo box choice */
	i = gtk_combo_box_get_active(GTK_COMBO_BOX(combo));

	/* Check for no campaign */
	if (!i)
	{
		/* Set description */
		gtk_label_set_text(GTK_LABEL(campaign_desc), "No campaign");
	}
	else
	{
		/* Set campaign description */
		gtk_label_set_text(GTK_LABEL(campaign_desc),
		                   camp_library[i - 1].desc);
	}
}

/*
 * Select a campaign to use.
 */
static void select_campaign(GtkMenuItem *menu_item, gpointer data)
{
	GtkWidget *dialog, *combo;
	GtkWidget *frame;
	int i;

	/* Check for connected to server */
	if (client_state != CS_DISCONN) return;

	/* Create dialog box */
	dialog = gtk_dialog_new_with_buttons("Select Campaign", NULL,
	                                     GTK_DIALOG_MODAL,
	                                     GTK_STOCK_OK,
	                                     GTK_RESPONSE_ACCEPT,
	                                     GTK_STOCK_CANCEL,
	                                     GTK_RESPONSE_REJECT, NULL);

	/* Set default width */
	gtk_window_set_default_size(GTK_WINDOW(dialog), 480, -1);

	/* Set window title */
	gtk_window_set_title(GTK_WINDOW(dialog),
	                     "Race for the Galaxy " VERSION);

	/* Set spacing */
	gtk_box_set_spacing(GTK_BOX(GTK_DIALOG(dialog)->vbox), 5);

	/* Create combo box */
	combo = gtk_combo_box_new_text();

	/* Add "no campaign" option */
	gtk_combo_box_append_text(GTK_COMBO_BOX(combo), "None");

	/* Assume no campaign */
	gtk_combo_box_set_active(GTK_COMBO_BOX(combo), 0);

	/* Loop over campaigns */
	for (i = 0; i < num_campaign; i++)
	{
		/* Add campaign name to combo box */
		gtk_combo_box_append_text(GTK_COMBO_BOX(combo),
		                          camp_library[i].name);

		/* Check for currently active */
		if (opt.campaign_name &&
		    !strcmp(opt.campaign_name, camp_library[i].name))
		{
			/* Set active */
			gtk_combo_box_set_active(GTK_COMBO_BOX(combo), i + 1);
		}
	}

	/* Add combo box to dialog */
	gtk_container_add(GTK_CONTAINER(GTK_DIALOG(dialog)->vbox), combo);

	/* Create frame to hold campaign description */
	frame = gtk_frame_new("Campaign description");

	/* Add frame to dialog */
	gtk_container_add(GTK_CONTAINER(GTK_DIALOG(dialog)->vbox), frame);

	/* Add campaign description label */
	campaign_desc = gtk_label_new("");

	/* Add label to dialog */
	gtk_container_add(GTK_CONTAINER(frame), campaign_desc);

	/* Add handler */
	g_signal_connect(G_OBJECT(combo), "changed",
			 G_CALLBACK(campaign_changed), NULL);

	/* Initialize description label */
	campaign_changed(GTK_COMBO_BOX(combo), NULL);

	/* Show all widgets */
	gtk_widget_show_all(dialog);

	/* Run dialog */
	if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT)
	{
		/* Get combo box choice */
		i = gtk_combo_box_get_active(GTK_COMBO_BOX(combo));

		/* Check for no campaign set */
		if (!i)
		{
			/* Clear campaign */
			opt.campaign_name = "";
		}
		else
		{
			/* Set campaign */
			opt.campaign_name = camp_library[i - 1].name;
		}

		/* Apply options */
		apply_options();

		/* Recreate GUI elements for new number of players */
		modify_gui(TRUE);

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
 * Choose a location for autosaves and saved logs.
 */
static void file_location_pressed(GtkButton *button, gpointer data)
{
	GtkWidget *dialog;
	char *folder_name;
	char **option = (char **) data;

	/* Create file chooser dialog box */
	dialog = gtk_file_chooser_dialog_new("Choose location", NULL,
	                                     GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER,
	                                     GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
	                                     GTK_STOCK_OPEN, GTK_RESPONSE_ACCEPT,
	                                     NULL);

	/* Check if last folder location is set */
	if (*option)
	{
		/* Set current folder to last save */
		gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(dialog), *option);
	}

	/* Run dialog and check response */
	if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT)
	{
		/* Get folder name */
		folder_name = gtk_file_chooser_get_current_folder(GTK_FILE_CHOOSER(dialog));

		/* Save folder name to options */
		*option = folder_name;
	}

	/* Destroy file choose dialog */
	gtk_widget_destroy(dialog);
}

/*
 * Modify GUI options.
 */
static void gui_options(GtkMenuItem *menu_item, gpointer data)
{
	GtkWidget *dialog;
	GtkWidget *status_box, *status_frame, *hide_card_button;
	GtkWidget *sizes_table, *card_size_label, *card_size_scale;
	GtkWidget *log_width_label, *log_width_scale;
	GtkWidget *game_view_box, *game_view_frame;
	GtkWidget *shrink_button, *discount_button, *hand_vp_button;
	GtkWidget *hand_cost_button, *key_cues_button;
	GtkWidget *interface_box, *interface_frame;
	GtkWidget *auto_select_button;
	GtkWidget *log_box, *log_frame;
	GtkWidget *colored_log_button, *verbose_button;
	GtkWidget *draw_log_button, *discard_log_button;

	options old_options = opt;

	/* Create dialog box */
	dialog = gtk_dialog_new_with_buttons("GUI Options", NULL,
	                                     GTK_DIALOG_MODAL,
	                                     GTK_STOCK_OK,
	                                     GTK_RESPONSE_ACCEPT,
	                                     GTK_STOCK_CANCEL,
	                                     GTK_RESPONSE_REJECT, NULL);

	/* Make some space */
	gtk_box_set_spacing(GTK_BOX(GTK_DIALOG(dialog)->vbox), 4);

	/* Set window title */
	gtk_window_set_title(GTK_WINDOW(dialog), TITLE);

	/* ---- Game status ---- */
	/* Create frame around buttons */
	status_frame = gtk_frame_new("Game status");

	/* Create vbox to hold game status options */
	status_box = gtk_vbox_new(FALSE, 0);

	/* Create toggle button for hiding card image */
	hide_card_button = gtk_check_button_new_with_label("Hide card");

	/* Set toggled status */
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(hide_card_button),
	                             opt.hide_card == 2);

	/* Pack button into box */
	gtk_box_pack_start(GTK_BOX(status_box), hide_card_button, FALSE, TRUE, 0);

	/* Create table to hold card size and log width widgets */
	sizes_table = gtk_table_new(2, 4, FALSE);

	/* Create card size label */
	card_size_label = gtk_label_new("Card size");

	/* Left align label */
	gtk_misc_set_alignment(GTK_MISC(card_size_label), 0.0, 0.5);

	/* Put label into table */
	gtk_table_attach(GTK_TABLE(sizes_table), card_size_label, 0, 1, 0, 1,
	                 GTK_FILL, GTK_FILL, 3, 0);

	/* Create card size scale */
	card_size_scale = gtk_hscale_new_with_range(150, CARD_WIDTH, 10);

	/* Do not display value */
	gtk_scale_set_draw_value(GTK_SCALE(card_size_scale), FALSE);

	/* Set value */
	gtk_range_set_value(GTK_RANGE(card_size_scale),
	                    opt.card_size ? opt.card_size : CARD_WIDTH);

	/* Set scale sensitivity status */
	gtk_widget_set_sensitive(GTK_WIDGET(card_size_scale),
	                         opt.hide_card != 2);

	/* Connect hide card "toggled" signal */
	g_signal_connect(G_OBJECT(hide_card_button), "toggled",
	                 G_CALLBACK(hide_card_changed), card_size_scale);

	/* Put scale into table */
	gtk_table_attach_defaults(GTK_TABLE(sizes_table), card_size_scale,
	                          1, 4, 0, 1);

	/* Create log width label */
	log_width_label = gtk_label_new("Log width");

	/* Left align label */
	gtk_misc_set_alignment(GTK_MISC(log_width_label), 0.0, 0.5);

	/* Put label into table */
	gtk_table_attach(GTK_TABLE(sizes_table), log_width_label, 0, 1, 1, 2,
	                 GTK_FILL, GTK_FILL, 3, 0);

	/* Create log width scale */
	log_width_scale = gtk_hscale_new_with_range(150, CARD_WIDTH * 2, 10);

	/* Do not display value */
	gtk_scale_set_draw_value(GTK_SCALE(log_width_scale), FALSE);

	/* Set default value for log width */
	if (!opt.log_width)
		opt.log_width = opt.card_size ? opt.card_size : CARD_WIDTH;

	/* Set value */
	gtk_range_set_value(GTK_RANGE(log_width_scale), opt.log_width);

	/* Put scale into table */
	gtk_table_attach_defaults(GTK_TABLE(sizes_table), log_width_scale,
	                          1, 4, 1, 2);

	/* Pack table into status box */
	gtk_container_add(GTK_CONTAINER(status_box), sizes_table);

	/* Create card size "value-changed" signal */
	g_signal_connect(G_OBJECT(card_size_scale), "value-changed",
	                 G_CALLBACK(card_size_changed), log_width_scale);

	/* Create log width "value-changed" signal */
	g_signal_connect(G_OBJECT(log_width_scale), "value-changed",
	                 G_CALLBACK(log_width_changed), card_size_scale);

	/* Simulate a value changed to force scale update if needed */
	card_size_changed(GTK_RANGE(card_size_scale), log_width_scale);

	/* Pack status box into status frame */
	gtk_container_add(GTK_CONTAINER(status_frame), status_box);

	/* Add status fram to dialog box */
	gtk_container_add(GTK_CONTAINER(GTK_DIALOG(dialog)->vbox),
	                  status_frame);

	/* ---- Game view ---- */
	/* Create vbox to hold game view check boxes */
	game_view_box = gtk_vbox_new(FALSE, 0);

	/* Create toggle button for shrinking opponent areas */
	shrink_button = gtk_check_button_new_with_label("Shrink Opponents");

	/* Set toggled status */
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(shrink_button),
	                             opt.shrink_opponent);

	/* Connect toggle button "toggled" signal */
	g_signal_connect(G_OBJECT(shrink_button), "toggled",
	                 G_CALLBACK(update_option), &opt.shrink_opponent);

	/* Pack button into box */
	gtk_box_pack_start(GTK_BOX(game_view_box), shrink_button, FALSE, TRUE, 0);

	/* Create toggle button for settle discount */
	discount_button = gtk_check_button_new_with_label(
	    "Display Settle discounts");

	/* Set toggled status */
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(discount_button),
	                             opt.settle_discount);

	/* Connect toggle button "toggled" signal */
	g_signal_connect(G_OBJECT(discount_button), "toggled",
	                 G_CALLBACK(update_option), &opt.settle_discount);

	/* Pack button into status box */
	gtk_box_pack_start(GTK_BOX(game_view_box), discount_button,
	                   FALSE, TRUE, 0);

	/* Create toggle button for hand VPs */
	hand_vp_button = gtk_check_button_new_with_label(
	    "Display VPs for cards in hand");

	/* Set toggled status */
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(hand_vp_button),
	                             opt.vp_in_hand);

	/* Connect toggle button "toggled" signal */
	g_signal_connect(G_OBJECT(hand_vp_button), "toggled",
	                 G_CALLBACK(update_option), &opt.vp_in_hand);

	/* Pack button into status box */
	gtk_box_pack_start(GTK_BOX(game_view_box), hand_vp_button, FALSE, TRUE, 0);

	/* Create toggle button for hand cost */
	hand_cost_button = gtk_check_button_new_with_label(
	    "Display costs during placement");

	/* Set toggled status */
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(hand_cost_button),
	                             opt.cost_in_hand);

	/* Connect toggle button "toggled" signal */
	g_signal_connect(G_OBJECT(hand_cost_button), "toggled",
	                 G_CALLBACK(update_option), &opt.cost_in_hand);

	/* Pack button into status box */
	gtk_box_pack_start(GTK_BOX(game_view_box), hand_cost_button, FALSE, TRUE, 0);

	/* Create toggle button for key cues */
	key_cues_button = gtk_check_button_new_with_label(
	    "Always display key cues");

	/* Set toggled status */
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(key_cues_button),
	                             opt.key_cues);

	/* Connect toggle button "toggled" signal */
	g_signal_connect(G_OBJECT(key_cues_button), "toggled",
	                 G_CALLBACK(update_option), &opt.key_cues);

	/* Pack button into status box */
	gtk_box_pack_start(GTK_BOX(game_view_box), key_cues_button, FALSE, TRUE, 0);

	/* Create frame around buttons */
	game_view_frame = gtk_frame_new("Game view");

	/* Pack button box into frame */
	gtk_container_add(GTK_CONTAINER(game_view_frame), game_view_box);

	/* Add frame to dialog box */
	gtk_container_add(GTK_CONTAINER(GTK_DIALOG(dialog)->vbox),
	                  game_view_frame);

	/* ---- Interface options ---- */
	/* Create vbox to hold interface check boxes */
	interface_box = gtk_vbox_new(FALSE, 0);

	/* Create toggle button for auto selection */
	auto_select_button =
		gtk_check_button_new_with_label("Auto select forced choices");

	/* Set toggled status */
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(auto_select_button),
	                             opt.auto_select);

	/* Connect toggle button "toggled" signal */
	g_signal_connect(G_OBJECT(auto_select_button), "toggled",
	                 G_CALLBACK(update_option), &opt.auto_select);

	/* Pack button into box */
	gtk_box_pack_start(GTK_BOX(interface_box), auto_select_button,
	                   FALSE, TRUE, 0);

	/* Create frame around buttons */
	interface_frame = gtk_frame_new("Interface options");

	/* Pack button box into frame */
	gtk_container_add(GTK_CONTAINER(interface_frame), interface_box);

	/* Add frame to dialog box */
	gtk_container_add(GTK_CONTAINER(GTK_DIALOG(dialog)->vbox),
	                  interface_frame);

	/* ---- Log options ---- */
	/* Create vbox to hold log check boxes */
	log_box = gtk_vbox_new(FALSE, 0);

	/* Create toggle button for colored log */
	colored_log_button = gtk_check_button_new_with_label("Colored log");

	/* Set toggled status */
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(colored_log_button),
	                             opt.colored_log);

	/* Pack button into box */
	gtk_box_pack_start(GTK_BOX(log_box), colored_log_button, FALSE, TRUE, 0);

	/* Create toggle button for verbose log */
	verbose_button = gtk_check_button_new_with_label("Verbose log");

	/* Set toggled status */
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(verbose_button),
	                             opt.verbose_log);

	/* Pack button into box */
	gtk_box_pack_start(GTK_BOX(log_box), verbose_button, FALSE, TRUE, 0);

	/* Create toggle button for draw log */
	draw_log_button = gtk_check_button_new_with_label(
		"Log drawn cards");

	/* Set toggled status */
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(draw_log_button),
	                             opt.draw_log);

	/* Pack button into box */
	gtk_box_pack_start(GTK_BOX(log_box), draw_log_button, FALSE, TRUE, 0);

	/* Create toggle button for discard log */
	discard_log_button = gtk_check_button_new_with_label(
		"Log discarded and saved cards");

	/* Set toggled status */
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(discard_log_button),
	                             opt.discard_log);

	/* Pack button into box */
	gtk_box_pack_start(GTK_BOX(log_box), discard_log_button, FALSE, TRUE, 0);

	/* Create frame around buttons */
	log_frame = gtk_frame_new("Log options");

	/* Pack button box into frame */
	gtk_container_add(GTK_CONTAINER(log_frame), log_box);

	/* Add frame to dialog box */
	gtk_container_add(GTK_CONTAINER(GTK_DIALOG(dialog)->vbox),
	                  log_frame);

	/* ---- End ---- */

	/* Show all widgets */
	gtk_widget_show_all(dialog);

	/* Run dialog */
	if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT)
	{
		/* Set colored log option */
		opt.colored_log =
		 gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(colored_log_button));

		/* Set verbose log option */
		opt.verbose_log =
		 gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(verbose_button));

		/* Set draw log option */
		opt.draw_log =
		 gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(draw_log_button));

		/* Set discard log option */
		opt.discard_log =
		 gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(discard_log_button));

		/* Save preferences */
		save_prefs();

		/* Restart main loop if not online and log options changed */
		if (client_state == CS_DISCONN &&
		    (opt.colored_log != old_options.colored_log ||
		     opt.verbose_log != old_options.verbose_log ||
		     opt.draw_log != old_options.draw_log ||
		     opt.discard_log != old_options.discard_log ||
		     opt.vp_in_hand != old_options.vp_in_hand ||
		     opt.cost_in_hand != old_options.cost_in_hand))
		{
			/* Force current game over */
			real_game.game_over = 1;

			/* Replay to current position */
			restart_loop = RESTART_CURRENT;

			/* Quit waiting for events */
			gtk_main_quit();
		}
	}
	else
	{
		/* Restore old options */
		opt = old_options;
	}

	/* Handle new options */
	modify_gui(FALSE);

	/* Redraw everything */
	redraw_everything();

	/* Destroy dialog */
	gtk_widget_destroy(dialog);
}

/*
 * Modify advanced options.
 */
static void advanced_options(GtkMenuItem *menu_item, gpointer data)
{
	GtkWidget *dialog;
	GtkWidget *autosave_box, *autosave_frame;
	GtkWidget *autosave_button, *autosave_location_button;
	GtkWidget *export_box, *export_frame;
	GtkWidget *style_sheet_box, *style_sheet_label, *style_sheet_entry;
	GtkWidget *autoexport_button;
	GtkWidget *autoexport_location_button;

	options old_options = opt;

	/* Create dialog box */
	dialog = gtk_dialog_new_with_buttons("Advanced options", NULL,
	                                     GTK_DIALOG_MODAL,
	                                     GTK_STOCK_OK,
	                                     GTK_RESPONSE_ACCEPT,
	                                     GTK_STOCK_CANCEL,
	                                     GTK_RESPONSE_REJECT, NULL);

	/* Make some space */
	gtk_box_set_spacing(GTK_BOX(GTK_DIALOG(dialog)->vbox), 4);

	/* Set window title */
	gtk_window_set_title(GTK_WINDOW(dialog), TITLE);

	/* ---- Autosave options ---- */
	/* Create vbox to hold file options check boxes */
	autosave_box = gtk_vbox_new(FALSE, 0);

	/* Create toggle button for autosaving */
	autosave_button = gtk_check_button_new_with_label("Autosave");

	/* Set toggled status */
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(autosave_button),
	                             opt.auto_save);

	/* Pack button into box */
	gtk_box_pack_start(GTK_BOX(autosave_box), autosave_button, FALSE, TRUE, 0);

	/* Create file location button */
	autosave_location_button =
		gtk_button_new_with_label("Choose autosave folder...");

	/* Attach event */
	g_signal_connect(G_OBJECT(autosave_location_button), "clicked",
	                 G_CALLBACK(file_location_pressed), &opt.data_folder);

	/* Pack button into box */
	gtk_box_pack_start(GTK_BOX(autosave_box), autosave_location_button,
	                   FALSE, TRUE, 0);

	/* Create frame around buttons */
	autosave_frame = gtk_frame_new("Autosave options");

	/* Pack button box into frame */
	gtk_container_add(GTK_CONTAINER(autosave_frame), autosave_box);

	/* Add frame to dialog box */
	gtk_container_add(GTK_CONTAINER(GTK_DIALOG(dialog)->vbox),
	                  autosave_frame);

	/* ---- Export options ---- */
	/* Create vbox to hold export widgets */
	export_box = gtk_vbox_new(FALSE, 0);

	/* Create hbox to hold style sheet label and entry */
	style_sheet_box = gtk_hbox_new(FALSE, 0);

	/* Create style sheet label */
	style_sheet_label = gtk_label_new("Export style sheet:");

	/* Pack name label into name box */
	gtk_box_pack_start(GTK_BOX(style_sheet_box), style_sheet_label,
	                   FALSE, TRUE, 5);

	/* Create text entry for style sheet */
	style_sheet_entry = gtk_entry_new();

	/* Set max length */
	gtk_entry_set_max_length(GTK_ENTRY(style_sheet_entry), 50);

	/* Set request size */
	gtk_widget_set_size_request(style_sheet_entry, 120, -1);

	/* Set style sheet contents */
	gtk_entry_set_text(GTK_ENTRY(style_sheet_entry),
	                   opt.export_style_sheet ? opt.export_style_sheet : "");

	/* Connect the style sheet entry's activate signal to */
	/* the accept response on the dialog */
	g_signal_connect(G_OBJECT(style_sheet_entry), "activate",
	                 G_CALLBACK(enter_callback), (gpointer) dialog);

	/* Pack style sheet entry into style sheet box */
	gtk_box_pack_start(GTK_BOX(style_sheet_box), style_sheet_entry,
	                   FALSE, TRUE, 0);

	/* Pack style sheet box into export box */
	gtk_container_add(GTK_CONTAINER(export_box), style_sheet_box);

	/* Create toggle button for auto-export */
	autoexport_button = gtk_check_button_new_with_label(
	    "Export game when finished");

	/* Set toggled status */
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(autoexport_button),
	                             opt.auto_export);

	/* Pack button into box */
	gtk_box_pack_start(GTK_BOX(export_box), autoexport_button, FALSE, TRUE, 0);

	/* Create file location button */
	autoexport_location_button =
		gtk_button_new_with_label("Choose export folder...");

	/* Attach event */
	g_signal_connect(G_OBJECT(autoexport_location_button), "clicked",
	                 G_CALLBACK(file_location_pressed), &opt.export_folder);

	/* Pack button into box */
	gtk_box_pack_start(GTK_BOX(export_box), autoexport_location_button,
	                   FALSE, TRUE, 0);

	/* Create frame around widgets */
	export_frame = gtk_frame_new("Export options");

	/* Pack export box into frame */
	gtk_container_add(GTK_CONTAINER(export_frame), export_box);

	/* Add frame to dialog box */
	gtk_container_add(GTK_CONTAINER(GTK_DIALOG(dialog)->vbox),
	                  export_frame);

	/* ---- End ---- */

	/* Show all widgets */
	gtk_widget_show_all(dialog);

	/* Run dialog */
	if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT)
	{
		/* Set autosave option */
		opt.auto_save =
		 gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(autosave_button));

		/* Set auto export option */
		opt.auto_export =
		 gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(autoexport_button));

		/* Set export style sheet option */
		opt.export_style_sheet =
		 strdup(gtk_entry_get_text(GTK_ENTRY(style_sheet_entry)));

		/* Save preferences */
		save_prefs();
	}
	else
	{
		/* Restore old options */
		opt = old_options;
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
	name = (i < 0 || i > 8) ? "Unknown" : location_names[i];

	/* Set "text" property of renderer */
	g_object_set(cell, "text", name, NULL);
}

/*
 * Attempt to place a moved card into the proper display table.
 */
static void debug_card_moved(int c, int owner, int where)
{
	card *c_ptr;
	displayed *i_ptr;
	int i, old_size;

	/* Get card pointer */
	c_ptr = &real_game.deck[c];

	/* Check for moving from our hand */
	if (c_ptr->owner == player_us && c_ptr->where == WHERE_HAND)
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
				old_size = --hand_size;
				hand[i] = hand[old_size];
				hand[old_size].tooltip = NULL;

				/* Sort hand */
				qsort(hand, hand_size, sizeof(displayed), cmp_hand);

				/* Done */
				break;
			}
		}
	}

	/* Check for moving from active area */
	if (c_ptr->where == WHERE_ACTIVE && c_ptr->owner != -1)
	{
		/* Loop over table area */
		for (i = 0; i < table_size[c_ptr->owner]; i++)
		{
			/* Get displayed card pointer */
			i_ptr = &table[c_ptr->owner][i];

			/* Check for match */
			if (i_ptr->index == c)
			{
				/* Remove from table */
				old_size = --table_size[c_ptr->owner];
				table[c_ptr->owner][i] = table[c_ptr->owner][old_size];
				table[c_ptr->owner][old_size].tooltip = NULL;

				/* Sort list */
				qsort(table[c_ptr->owner], table_size[c_ptr->owner],
				      sizeof(displayed), cmp_table);

				/* Done */
				break;
			}
		}
	}

	/* Check for adding card to our hand */
	if (owner == player_us && where == WHERE_HAND)
	{
		/* Add card to hand */
		i_ptr = &hand[hand_size++];

		/* Set design and index */
		i_ptr->d_ptr = c_ptr->d_ptr;
		i_ptr->index = c;

		/* Clear all other fields */
		i_ptr->eligible = i_ptr->gapped = 0;
		i_ptr->selected = i_ptr->color = 0;
		i_ptr->num_goods = 0;
		i_ptr->order = -1;

		/* Generate tool tip */
		i_ptr->tooltip = card_hand_tooltip(&real_game, player_us, c);

		/* Sort hand */
		qsort(hand, hand_size, sizeof(displayed), cmp_hand);
	}

	/* Check for adding card to active area */
	if (owner != -1 && where == WHERE_ACTIVE)
	{
		/* Add card to hand */
		i_ptr = &table[owner][table_size[owner]++];

		/* Set design and index */
		i_ptr->d_ptr = c_ptr->d_ptr;
		i_ptr->index = c;

		/* Clear all other fields */
		i_ptr->eligible = i_ptr->gapped = 0;
		i_ptr->selected = i_ptr->color = 0;
		i_ptr->num_goods = 0;
		i_ptr->order = -1;

		/* Generate tool tip */
		i_ptr->tooltip = card_table_tooltip(&real_game, player_us, c);

		/* Sort list */
		qsort(table[owner], table_size[owner],
		      sizeof(displayed), cmp_table);
	}
}

/*
 * The card list store of the debug dialog.
 */
static GtkListStore *card_list;

/*
 * The column for the last changed cell in the debug dialog.
 */
static int new_column;

/*
 * The path for the last changed cell in the debug dialog.
 */
static GtkTreePath *new_path;

/*
 * The current value for the last changed cell in the debug dialog.
 */
static int new_value;

/*
 * Called when the player cell of the debug window has been changed.
 */
static void player_changed(GtkCellRendererCombo *cell, char *path_str,
                           GtkTreeIter *new_iter, gpointer data)
{
	/* Save the column */
	new_column = 2;

	/* Save path from path string */
	new_path = gtk_tree_path_new_from_string(path_str);

	/* Save the new value */
	gtk_tree_model_get(GTK_TREE_MODEL(data), new_iter, 0, &new_value, -1);
}

/*
 * Called when the where cell of the debug window has been changed.
 */
static void where_changed(GtkCellRendererCombo *cell, char *path_str,
                          GtkTreeIter *new_iter, gpointer data)
{
	/* Save the column */
	new_column = 3;

	/* Save path from path string */
	new_path = gtk_tree_path_new_from_string(path_str);

	/* Save the new value */
	gtk_tree_model_get(GTK_TREE_MODEL(data), new_iter, 0, &new_value, -1);
}

/*
 * Called when a cell in the debug window has been edited.
 */
static void debug_edit(GtkCellRendererCombo *cell, char *path_str, char *text,
                       gpointer data)
{
	GtkTreeModel *model = GTK_TREE_MODEL(card_list);
	GtkTreePath *path;
	GtkTreeIter iter;
	int column = GPOINTER_TO_INT(data);

	/* No changes */
	if (!new_path) return;

	/* Create path from path string */
	path = gtk_tree_path_new_from_string(path_str);

	/* Get iterator for path */
	gtk_tree_model_get_iter(model, &iter, path);

	/* Store new value in model */
	gtk_list_store_set(GTK_LIST_STORE(model), &iter, column, new_value, -1);

	/* Clear the current path */
	new_path = NULL;
}

/*
 * Called when editing a cell in the debug window has been canceled.
 */
static void debug_canceled(GtkCellRendererCombo *cell, gpointer data)
{
	GtkTreeModel *model = GTK_TREE_MODEL(card_list);
	GtkTreeIter iter;
	int column = GPOINTER_TO_INT(data);

	/* No changes */
	if (!new_path) return;

	/* Get iterator for path */
	gtk_tree_model_get_iter(model, &iter, new_path);

	/* Store new value in model (i.e. ignore the cancel event) */
	gtk_list_store_set(GTK_LIST_STORE(model), &iter, column, new_value, -1);

	/* Clear the current path */
	new_path = NULL;
}

/*
 * Called on each row of the debug_card_dialog after it has been
 * successfully closed. Will add DEBUG_CHOICE values to the log if
 * card locations has been changed.
 */
static int debug_update_card(GtkTreeModel *model, GtkTreePath *path,
                             GtkTreeIter *iter, gpointer data)
{
	int c, owner, where;
	player *p_ptr;
	card *c_ptr;
	int *l_ptr;

	/* Get row values */
	gtk_tree_model_get(model, iter, 0, &c, 2, &owner, 3, &where, -1);

	/* Get card */
	c_ptr = &real_game.deck[c];

	/* Check for changed location */
	if (c_ptr->owner != owner || c_ptr->where != where)
	{
		/* Get player pointer */
		p_ptr = &real_game.p[player_us];

		/* Get pointer to end of choice log */
		l_ptr = &p_ptr->choice_log[p_ptr->choice_size];

		/* Add debug choice type to log */
		*l_ptr++ = CHOICE_DEBUG;

		/* Add card index to log */
		*l_ptr++ = c;

		/* Add data size to log */
		*l_ptr++ = 2;

		/* Add new card owner to log */
		*l_ptr++ = owner;

		/* Add new card position to log */
		*l_ptr++ = where;

		/* Add empty special list */
		*l_ptr++ = 0;

		/* Mark new size of choice log */
		p_ptr->choice_size = l_ptr - p_ptr->choice_log;

		/* Mark one choice done */
		choice_done(&real_game);

		/* Check for local game */
		if (client_state == CS_DISCONN)
		{
			/* Move card to its proper location immediately */
			debug_card_moved(c, owner, where);
		}
	}

	/* Continue the foreach loop */
	return FALSE;
}

/*
 * Show a "debug" dialog to give players cards, etc.
 */
static void debug_card_dialog(GtkMenuItem *menu_item, gpointer data)
{
	GtkWidget *dialog;
	GtkWidget *list_view, *list_scroll;
	GtkListStore *player_list, *where_list;
	GtkTreeViewColumn *tree_view_column;
	GtkTreeIter list_iter;
	GtkCellRenderer *render;
	card *c_ptr;
	int i;

	/* Check for connected to non-debug server */
	if (client_state != CS_DISCONN && !debug_server) return;

	/* Set the tampered look flag */
	game_tampered |= TAMPERED_LOOK;

	/* Create dialog box */
	dialog = gtk_dialog_new_with_buttons("Debug", NULL, 0,
	                                     GTK_STOCK_OK,
	                                     GTK_RESPONSE_ACCEPT,
	                                     GTK_STOCK_CANCEL,
	                                     GTK_RESPONSE_REJECT, NULL);

	/* Set window title */
	gtk_window_set_title(GTK_WINDOW(dialog), TITLE);

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


	// TODO: Loop over location_names
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

	/* Add row for "Saved" */
	gtk_list_store_append(where_list, &list_iter);
	gtk_list_store_set(where_list, &list_iter,
	                   0, WHERE_SAVED, 1, "Saved", -1);

	/* Add row for "Aside" */
	gtk_list_store_append(where_list, &list_iter);
	gtk_list_store_set(where_list, &list_iter,
	                   0, WHERE_ASIDE, 1, "Aside", -1);

	/* Add row for "Campaign" */
	gtk_list_store_append(where_list, &list_iter);
	gtk_list_store_set(where_list, &list_iter,
	                   0, WHERE_CAMPAIGN, 1, "Campaign", -1);

	/* Create view of card list */
	list_view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(card_list));

	/*** First column (card name) ***/

	/* Create text renderer */
	render = gtk_cell_renderer_text_new();

	/* Create list view column */
	gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(list_view),
	                                            -1, "Card Name", render,
	                                            "text", 1, NULL);

	/* Retrieve the new column */
	tree_view_column = gtk_tree_view_get_column(GTK_TREE_VIEW(list_view), 0);

	/* Set the column to sort on */
	gtk_tree_view_column_set_sort_column_id(tree_view_column, 1);

	/*** Second column (card owner) ***/

	/* Create combo box renderer */
	render = gtk_cell_renderer_combo_new();

	/* Set renderer properties */
	g_object_set(render, "text-column", 1, "model", player_list,
	             "editable", TRUE, "has-entry", FALSE, NULL);

	/* Connect "changed" signal */
	g_signal_connect(render, "changed", G_CALLBACK(player_changed),
	                 player_list);

	/* Connect "edited" signal */
	g_signal_connect(render, "edited", G_CALLBACK(debug_edit),
	                 GINT_TO_POINTER(2));

	/* Connect "editing-canceled" signal */
	g_signal_connect(render, "editing-canceled", G_CALLBACK(debug_canceled),
	                 GINT_TO_POINTER(2));

	/* Create list view column */
	gtk_tree_view_insert_column_with_data_func(GTK_TREE_VIEW(list_view),
	                                           -1, "Owner", render,
	                                           render_player, NULL,
	                                           NULL);

	/* Retrieve the new column */
	tree_view_column = gtk_tree_view_get_column(GTK_TREE_VIEW(list_view), 1);

	/* Set the column to sort on */
	gtk_tree_view_column_set_sort_column_id(tree_view_column, 2);

	/*** Third column (card location) ***/

	/* Create combo box renderer */
	render = gtk_cell_renderer_combo_new();

	/* Set renderer properties */
	g_object_set(render, "text-column", 1, "model", where_list,
	             "editable", TRUE, "has-entry", FALSE, NULL);

	/* Connect "changed" signal */
	g_signal_connect(render, "changed", G_CALLBACK(where_changed), where_list);

	/* Connect "edited" signal */
	g_signal_connect(render, "edited", G_CALLBACK(debug_edit),
	                 GINT_TO_POINTER(3));

	/* Connect "editing-canceled" signal */
	g_signal_connect(render, "editing-canceled", G_CALLBACK(debug_canceled),
	                 GINT_TO_POINTER(3));

	/* Create list view column */
	gtk_tree_view_insert_column_with_data_func(GTK_TREE_VIEW(list_view),
	                                           -1, "Location", render,
	                                           render_where, NULL,
	                                           NULL);

	/* Retrieve the new column */
	tree_view_column = gtk_tree_view_get_column(GTK_TREE_VIEW(list_view), 2);

	/* Set the column to sort on */
	gtk_tree_view_column_set_sort_column_id(tree_view_column, 3);

	/* Enable interactive search on first column */
	gtk_tree_view_set_search_column(GTK_TREE_VIEW(list_view), 1);

	/* Create scrolled window for list view */
	list_scroll = gtk_scrolled_window_new(NULL, NULL);

	/* Add list view to scrolled window */
	gtk_container_add(GTK_CONTAINER(list_scroll), list_view);

	/* Set scrolling policy */
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(list_scroll),
	                               GTK_POLICY_NEVER,
	                               GTK_POLICY_ALWAYS);

	/* Add scrollable list view to dialog */
	gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dialog)->vbox), list_scroll,
	                   TRUE, TRUE, 0);

	/* Show everything */
	gtk_widget_show_all(dialog);

	/* Run dialog */
	if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT)
	{
		/* Simulate a canceled event (otherwise it would occur too late) */
		debug_canceled(NULL, GINT_TO_POINTER(new_column));

		/* Loop over list and potentially move cards */
		gtk_tree_model_foreach(GTK_TREE_MODEL(card_list), debug_update_card,
		                       NULL);

		/* Check for local game */
		if (client_state == CS_DISCONN)
		{
			/* Execute the debug moves immediately */
			perform_debug_moves(&real_game, player_us);

			/* Redraw table and hand */
			redraw_everything();
		}

		/* Update menu items */
		update_menu_items();
	}

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
		"Produce",
		"Search",
		"Prestige Explore +5",
		"Prestige Explore +1,+1",
		"Prestige Develop",
		"Prestige Settle",
		"Prestige Consume-Trade",
		"Prestige Consume-x2",
		"Prestige Produce",
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
		"C2/P",
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
	gtk_window_set_title(GTK_WINDOW(dialog), TITLE);

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
	                                TITLE);

	/* Set window title */
	gtk_window_set_title(GTK_WINDOW(dialog), TITLE);

	/* Set secondary text */
	gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog),
"This program is written by Keldon Jones, and the source code is licensed \
under the GNU General Public License.\n\n\
The interface enhancement release " RELEASE " is written by B. Nordli.\n\n\
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
	int i, waiting_for_server = FALSE;
	char *msg;

	/* Move text separator to bottom */
	reset_text_separator();

	/* Disable action button */
	gtk_widget_set_sensitive(action_button, FALSE);

	/* Check if we are online */
	if (client_state != CS_DISCONN)
	{
		/* Assume we are waiting for the server */
		waiting_for_server = TRUE;

		/* Loop over players */
		for (i = 0; i < real_game.num_players; ++i)
		{
			/* Check if we are waiting for the player */
			if (i != player_us && waiting_player[i] == WAIT_BLOCKED)
			{
				/* Remember we are waiting for a player */
				waiting_for_server = FALSE;
				break;
			}
		}
	}

	/* Select string */
	if (waiting_for_server) msg = "Waiting for server";
	else if (real_game.num_players == 2) msg = "Waiting for opponent";
	else msg = "Waiting for opponents";

	/* Reset action prompt */
	gtk_label_set_text(GTK_LABEL(action_prompt), msg);

	/* Quit innermost loop */
	gtk_main_quit();

	/* Handle pending events */
	while (gtk_events_pending()) gtk_main_iteration();
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
	GtkWidget *menu_bar;
	GtkWidget *game_menu, *undo_menu, *network_menu, *debug_menu, *help_menu;
	GtkWidget *game_item, *undo_m_item, *network_item, *debug_item, *help_item;
	GtkWidget *campaign_item;
	GtkWidget *h_sep, *v_sep, *event;
	GtkWidget *msg_scroll;
	GtkWidget *table_box, *active_box;
	GtkWidget *top_box, *top_view, *top_scroll, *area;
	GtkWidget *label;
	guint accel_key;
	GdkModifierType accel_mod;
	GtkSizeGroup *top_size_group;
	GtkTextIter end_iter;
	GtkTextBuffer *message_buffer, *chat_buffer;
	GtkCellRenderer *render, *toggle_render;
	GtkTreeViewColumn *desc_column;
	GdkColor color;

	char *fname = NULL;
	char msg[1024];
	int i, err;

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
	err = read_cards(NULL);

	/* Check for errors */
	if (err == -1)
	{
		/* Print error and exit */
		display_error("Error: Could not locate cards.txt!\n");
		exit(1);
	}
	else if (err == -2)
	{
		/* Print error and exit */
		display_error("Error: Could not parse cards.txt!\n");
		exit(1);
	}

	/* Load campaigns */
	read_campaign();

	/* Load card images */
	if (load_images() == -1)
	{
		/* Error */
		exit(1);
	}

	/* Read preference file */
	read_prefs();

	/* By default restore single-player game */
	restart_loop = RESTART_RESTORE;

	/* Parse arguments */
	for (i = 1; i < argc; i++)
	{
		/* Check for number of players */
		if (!strcmp(argv[i], "-p"))
		{
			/* Set number of players */
			opt.num_players = atoi(argv[++i]);

			/* Start new game */
			restart_loop = RESTART_NEW;
		}

		/* Check for expansion level */
		else if (!strcmp(argv[i], "-e"))
		{
			/* Set expansion level */
			opt.expanded = atoi(argv[++i]);

			/* Start new game */
			restart_loop = RESTART_NEW;
		}

		/* Check for player name */
		else if (!strcmp(argv[i], "-n"))
		{
			/* Set player name */
			opt.player_name = argv[++i];

			/* Start new game */
			restart_loop = RESTART_NEW;
		}

		/* Check for advanced game */
		else if (!strcmp(argv[i], "-a"))
		{
			/* Set advanced */
			opt.advanced = 1;

			/* Start new game */
			restart_loop = RESTART_NEW;
		}

		/* Check for random seed */
		else if (!strcmp(argv[i], "-r"))
		{
			/* Set random seed */
			opt.customize_seed = TRUE;

			/* Set start seed */
			opt.seed = (unsigned int) atof(argv[++i]);

			/* Start new game */
			restart_loop = RESTART_NEW;
		}

		/* Check for goals on */
		else if (!strcmp(argv[i], "-g"))
		{
			/* Set goals on */
			opt.disable_goal = FALSE;

			/* Start new game */
			restart_loop = RESTART_NEW;
		}

		/* Check for goals off */
		else if (!strcmp(argv[i], "-nog"))
		{
			/* Set goals off */
			opt.disable_goal = TRUE;

			/* Start new game */
			restart_loop = RESTART_NEW;
		}

		/* Check for takeovers on */
		else if (!strcmp(argv[i], "-t"))
		{
			/* Set takeovers on */
			opt.disable_takeover = FALSE;

			/* Start new game */
			restart_loop = RESTART_NEW;
		}

		/* Check for takeovers off */
		else if (!strcmp(argv[i], "-not"))
		{
			/* Set takeovers off */
			opt.disable_takeover = TRUE;

			/* Start new game */
			restart_loop = RESTART_NEW;
		}

		/* Check for saved game */
		else if (!strcmp(argv[i], "-s"))
		{
			/* Set file name */
			fname = argv[++i];
		}
	}

	/* Apply options */
	apply_options();

	/* Create choice logs for each player */
	for (i = 0; i < MAX_PLAYER; i++)
	{
		/* Create log */
		real_game.p[i].choice_log = (int *)malloc(sizeof(int) * 4096);

		/* Save original log */
		orig_log[i] = real_game.p[i].choice_log;
		orig_log_size[i] = 0;

		/* Clear choice log size and position */
		real_game.p[i].choice_size = 0;
		real_game.p[i].choice_pos = 0;
	}

	/* Create toplevel window */
	window = gtk_window_new(GTK_WINDOW_TOPLEVEL);

	/* Window default size */
	gtk_window_set_default_size(GTK_WINDOW(window), 1280, 800);

	/* Set window title */
	gtk_window_set_title(GTK_WINDOW(window), TITLE);

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
	g_signal_new("key-signal", gtk_toggle_button_get_type(), G_SIGNAL_ACTION,
	             0, NULL, NULL, g_cclosure_marshal_VOID__VOID, G_TYPE_NONE,
	             0);
	g_signal_new("key-signal", gtk_button_get_type(), G_SIGNAL_ACTION,
	             0, NULL, NULL, g_cclosure_marshal_VOID__VOID, G_TYPE_NONE,
	             0);
	g_signal_new("key-signal", gtk_combo_box_get_type(), G_SIGNAL_ACTION,
	             0, NULL, NULL, g_cclosure_marshal_VOID__VOID, G_TYPE_NONE,
	             0);
	g_signal_new("up-signal", gtk_combo_box_get_type(), G_SIGNAL_ACTION,
	             0, NULL, NULL, g_cclosure_marshal_VOID__VOID, G_TYPE_NONE,
	             0);
	g_signal_new("down-signal", gtk_combo_box_get_type(), G_SIGNAL_ACTION,
	             0, NULL, NULL, g_cclosure_marshal_VOID__VOID, G_TYPE_NONE,
	             0);

	/* Create select/deselect all signals */
	g_signal_new("key-select-all", gtk_event_box_get_type(), G_SIGNAL_ACTION,
	             0, NULL, NULL, g_cclosure_marshal_VOID__VOID, G_TYPE_NONE,
	             0);
	g_signal_new("key-deselect-all", gtk_event_box_get_type(), G_SIGNAL_ACTION,
	             0, NULL, NULL, g_cclosure_marshal_VOID__VOID, G_TYPE_NONE,
	             0);

	/* Loop over accelerator keys */
	for (i = 0; i < MAX_ACCEL; ++i)
	{
		/* Create key description */
		sprintf(msg, "%sF%d", i > 8 ? "<Shift>" : "", (i % 9) + 1);

		/* Parse key */
		gtk_accelerator_parse(msg, &accel_keys[i], &accel_mods[i]);

		/* Create key description */
		sprintf(card_extra_info[i].text,
		        "<b><span background=\"black\" foreground=\"white\">"
		        "%sF%d</span></b>",
		        i > 8 ? "S+" : "", (i % 9) + 1);

		/* Set font */
		card_extra_info[i].fontstr = "Sans 8";

		/* Set position */
		card_extra_info[i].top_left = 1;
	}

	/* Create main vbox to hold menu bar, then rest of game area */
	main_vbox = gtk_vbox_new(FALSE, 0);

	/* Create menu bar */
	menu_bar = gtk_menu_bar_new();

	/* Create menu item for 'game' menu */
	game_item = gtk_menu_item_new_with_mnemonic("_Game");

	/* Add game item to menu bar */
	gtk_menu_shell_append(GTK_MENU_SHELL(menu_bar), game_item);

	/* Create menu item for 'undo' menu */
	undo_m_item = gtk_menu_item_new_with_mnemonic("_Undo");

	/* Add undo item to menu bar */
	gtk_menu_shell_append(GTK_MENU_SHELL(menu_bar), undo_m_item);

	/* Create menu item for 'network' menu */
	network_item = gtk_menu_item_new_with_mnemonic("_Network");

	/* Add network item to menu bar */
	gtk_menu_shell_append(GTK_MENU_SHELL(menu_bar), network_item);

	/* Create menu item for 'debug' menu */
	debug_item = gtk_menu_item_new_with_mnemonic("_Debug");

	/* Add debug item to menu bar */
	gtk_menu_shell_append(GTK_MENU_SHELL(menu_bar), debug_item);

	/* Create menu item for 'help' menu */
	help_item = gtk_menu_item_new_with_mnemonic("_Help");

	/* Add help item to menu bar */
	gtk_menu_shell_append(GTK_MENU_SHELL(menu_bar), help_item);

	/* Create game menu */
	game_menu = gtk_menu_new();

	/* Create game menu items */
	new_item = gtk_menu_item_new_with_mnemonic("_New");
	new_parameters_item = gtk_menu_item_new_with_mnemonic("N_ew...");
	campaign_item = gtk_menu_item_new_with_mnemonic("Select _Campaign...");
	load_item = gtk_menu_item_new_with_mnemonic("_Load Game...");
	replay_item = gtk_menu_item_new_with_mnemonic("Re_play Game...");
	save_item = gtk_menu_item_new_with_mnemonic("_Save Game...");
	export_item = gtk_menu_item_new_with_mnemonic("E_xport to XML...");
	option_item = gtk_menu_item_new_with_mnemonic("_GUI Options...");
	advanced_item = gtk_menu_item_new_with_mnemonic("_Advanced Options...");
	quit_item = gtk_menu_item_new_with_mnemonic("_Quit");

	/* Add accelerators for game menu items */
	gtk_widget_add_accelerator(new_item, "activate", window_accel,
	                           'N', GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);
	gtk_widget_add_accelerator(new_parameters_item, "activate", window_accel,
	                           'N', GDK_SHIFT_MASK | GDK_CONTROL_MASK,
	                           GTK_ACCEL_VISIBLE);
	gtk_widget_add_accelerator(campaign_item, "activate", window_accel,
	                           'C', GDK_SHIFT_MASK | GDK_CONTROL_MASK,
	                           GTK_ACCEL_VISIBLE);
	gtk_widget_add_accelerator(load_item, "activate", window_accel,
	                           'L', GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);
	gtk_widget_add_accelerator(replay_item, "activate", window_accel,
	                           'P', GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);
	gtk_widget_add_accelerator(save_item, "activate", window_accel,
	                           'S', GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);
	gtk_widget_add_accelerator(export_item, "activate", window_accel,
	                           'E', GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);
	gtk_widget_add_accelerator(option_item, "activate", window_accel,
	                           'G', GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);
	gtk_widget_add_accelerator(advanced_item, "activate", window_accel,
	                           'A', GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);
	gtk_widget_add_accelerator(quit_item, "activate", window_accel,
	                           'Q', GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);

	/* Add items to game menu */
	gtk_menu_shell_append(GTK_MENU_SHELL(game_menu), new_item);
	gtk_menu_shell_append(GTK_MENU_SHELL(game_menu), new_parameters_item);
	gtk_menu_shell_append(GTK_MENU_SHELL(game_menu), campaign_item);
	gtk_menu_shell_append(GTK_MENU_SHELL(game_menu), load_item);
	gtk_menu_shell_append(GTK_MENU_SHELL(game_menu), replay_item);
	gtk_menu_shell_append(GTK_MENU_SHELL(game_menu), save_item);
	gtk_menu_shell_append(GTK_MENU_SHELL(game_menu), export_item);
	gtk_menu_shell_append(GTK_MENU_SHELL(game_menu), option_item);
	gtk_menu_shell_append(GTK_MENU_SHELL(game_menu), advanced_item);
	gtk_menu_shell_append(GTK_MENU_SHELL(game_menu), quit_item);

	/* Create undo menu */
	undo_menu = gtk_menu_new();

	/* Create undo menu items */
	undo_item = gtk_menu_item_new_with_mnemonic("_Undo");
	undo_round_item = gtk_menu_item_new_with_mnemonic("Undo _Round");
	undo_game_item = gtk_menu_item_new_with_mnemonic("Undo _Game");
	redo_item = gtk_menu_item_new_with_mnemonic("Re_do");
	redo_round_item = gtk_menu_item_new_with_mnemonic("Redo R_ound");
	redo_game_item = gtk_menu_item_new_with_mnemonic("Redo G_ame");

	/* Add accelerators for undo menu items */
	gtk_widget_add_accelerator(undo_item, "activate", window_accel,
	                           'Z', GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);
	gtk_widget_add_accelerator(undo_round_item, "activate", window_accel,
	                           'Z', GDK_SHIFT_MASK | GDK_CONTROL_MASK,
	                           GTK_ACCEL_VISIBLE);
	gtk_widget_add_accelerator(redo_item, "activate", window_accel,
	                           'Y', GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);
	gtk_widget_add_accelerator(redo_round_item, "activate", window_accel,
	                           'Y', GDK_SHIFT_MASK | GDK_CONTROL_MASK,
	                           GTK_ACCEL_VISIBLE);

	/* Add items to undo menu */
	gtk_menu_shell_append(GTK_MENU_SHELL(undo_menu), undo_item);
	gtk_menu_shell_append(GTK_MENU_SHELL(undo_menu), undo_round_item);
	gtk_menu_shell_append(GTK_MENU_SHELL(undo_menu), undo_game_item);
	gtk_menu_shell_append(GTK_MENU_SHELL(undo_menu), redo_item);
	gtk_menu_shell_append(GTK_MENU_SHELL(undo_menu), redo_round_item);
	gtk_menu_shell_append(GTK_MENU_SHELL(undo_menu), redo_game_item);

	/* Create network menu */
	network_menu = gtk_menu_new();

	/* Create network menu items */
	connect_item = gtk_menu_item_new_with_mnemonic("Connect to se_rver...");
	disconnect_item = gtk_menu_item_new_with_mnemonic("_Disconnect");
	resign_item = gtk_menu_item_new_with_mnemonic("_Resign from game");

	/* Add accelerators for network menu items */
	gtk_widget_add_accelerator(connect_item, "activate", window_accel,
	                           'R', GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);
	gtk_widget_add_accelerator(disconnect_item, "activate", window_accel,
	                           'D', GDK_SHIFT_MASK | GDK_CONTROL_MASK,
	                           GTK_ACCEL_VISIBLE);
	gtk_widget_add_accelerator(resign_item, "activate", window_accel,
	                           'R', GDK_SHIFT_MASK | GDK_CONTROL_MASK,
	                           GTK_ACCEL_VISIBLE);

	/* Add items to network menu */
	gtk_menu_shell_append(GTK_MENU_SHELL(network_menu), connect_item);
	gtk_menu_shell_append(GTK_MENU_SHELL(network_menu), disconnect_item);
	gtk_menu_shell_append(GTK_MENU_SHELL(network_menu), resign_item);

	/* Create debug menu */
	debug_menu = gtk_menu_new();

	/* Create debug menu items */
	debug_card_item = gtk_menu_item_new_with_mnemonic("Debug _cards...");
	debug_ai_item = gtk_menu_item_new_with_mnemonic("Debug _AI...");

	/* Add accelerators for debug menu items */
	gtk_widget_add_accelerator(debug_card_item, "activate", window_accel,
	                           'D', GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);

	/* Add items to debug menu */
	gtk_menu_shell_append(GTK_MENU_SHELL(debug_menu), debug_card_item);
	/* gtk_menu_shell_append(GTK_MENU_SHELL(debug_menu), debug_ai_item); */

	/* Create help menu */
	help_menu = gtk_menu_new();

	/* Create about menu item */
	about_item = gtk_menu_item_new_with_mnemonic("_About...");

	/* Parse the Ctrl+F1 key */
	gtk_accelerator_parse("<Control>F1", &accel_key, &accel_mod);

	/* Add accelerators for about menu items */
	gtk_widget_add_accelerator(about_item, "activate", window_accel,
	                           accel_key, accel_mod, GTK_ACCEL_VISIBLE);

	/* Add item to help menu */
	gtk_menu_shell_append(GTK_MENU_SHELL(help_menu), about_item);

	/* Attach events to menu items */
	g_signal_connect(G_OBJECT(new_item), "activate",
	                 G_CALLBACK(gui_new_game), NULL);
	g_signal_connect(G_OBJECT(new_parameters_item), "activate",
	                 G_CALLBACK(gui_new_parameters), NULL);
	g_signal_connect(G_OBJECT(campaign_item), "activate",
	                 G_CALLBACK(select_campaign), NULL);
	g_signal_connect(G_OBJECT(load_item), "activate",
	                 G_CALLBACK(gui_load_game),
	                 GINT_TO_POINTER(RESTART_LOAD));
	g_signal_connect(G_OBJECT(replay_item), "activate",
	                 G_CALLBACK(gui_load_game),
	                 GINT_TO_POINTER(RESTART_REPLAY));
	g_signal_connect(G_OBJECT(save_item), "activate",
	                 G_CALLBACK(gui_save_game), NULL);
	g_signal_connect(G_OBJECT(export_item), "activate",
	                 G_CALLBACK(gui_export_game), NULL);
	g_signal_connect(G_OBJECT(option_item), "activate",
	                 G_CALLBACK(gui_options), NULL);
	g_signal_connect(G_OBJECT(advanced_item), "activate",
	                 G_CALLBACK(advanced_options), NULL);
	g_signal_connect(G_OBJECT(quit_item), "activate",
	                 G_CALLBACK(gui_quit_game), NULL);

	g_signal_connect(G_OBJECT(undo_item), "activate",
	                 G_CALLBACK(gui_undo), GINT_TO_POINTER(RESTART_UNDO));
	g_signal_connect(G_OBJECT(undo_round_item), "activate",
	                 G_CALLBACK(gui_undo), GINT_TO_POINTER(RESTART_UNDO_ROUND));
	g_signal_connect(G_OBJECT(undo_game_item), "activate",
	                 G_CALLBACK(gui_undo), GINT_TO_POINTER(RESTART_UNDO_GAME));
	g_signal_connect(G_OBJECT(redo_item), "activate",
	                 G_CALLBACK(gui_redo), GINT_TO_POINTER(RESTART_REDO));
	g_signal_connect(G_OBJECT(redo_round_item), "activate",
	                 G_CALLBACK(gui_redo), GINT_TO_POINTER(RESTART_REDO_ROUND));
	g_signal_connect(G_OBJECT(redo_game_item), "activate",
	                 G_CALLBACK(gui_redo), GINT_TO_POINTER(RESTART_REDO_GAME));

	g_signal_connect(G_OBJECT(connect_item), "activate",
	                 G_CALLBACK(connect_dialog), NULL);
	g_signal_connect(G_OBJECT(disconnect_item), "activate",
	                 G_CALLBACK(disconnect_server), NULL);
	g_signal_connect(G_OBJECT(resign_item), "activate",
	                 G_CALLBACK(resign_game), NULL);

	g_signal_connect(G_OBJECT(debug_card_item), "activate",
	                 G_CALLBACK(debug_card_dialog), NULL);
	g_signal_connect(G_OBJECT(debug_ai_item), "activate",
	                 G_CALLBACK(debug_ai_dialog), NULL);

	g_signal_connect(G_OBJECT(about_item), "activate",
	                 G_CALLBACK(about_dialog), NULL);

	/* Set submenus */
	gtk_menu_item_set_submenu(GTK_MENU_ITEM(game_item), game_menu);
	gtk_menu_item_set_submenu(GTK_MENU_ITEM(undo_m_item), undo_menu);
	gtk_menu_item_set_submenu(GTK_MENU_ITEM(network_item), network_menu);
	gtk_menu_item_set_submenu(GTK_MENU_ITEM(debug_item), debug_menu);
	gtk_menu_item_set_submenu(GTK_MENU_ITEM(help_item), help_menu);

	/* Create main hbox to contain status box and game area box */
	main_hbox = gtk_hbox_new(FALSE, 0);

	/* Create left vbox for status information */
	left_vbox = gtk_vbox_new(FALSE, 0);

	/* Create "card view" image */
	full_image = gtk_image_new();

	/* Create separator for image box */
	h_sep = gtk_hseparator_new();

	/* Pack image and separator into left vbox */
	gtk_box_pack_start(GTK_BOX(left_vbox), full_image, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(left_vbox), h_sep, FALSE, FALSE, 0);

	/* Create game status label */
	game_status = gtk_hbox_new(TRUE, 0);

	/* Create separator for status info */
	h_sep = gtk_hseparator_new();

	/* Add game status to status vbox */
	gtk_box_pack_start(GTK_BOX(left_vbox), game_status, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(left_vbox), h_sep, FALSE, FALSE, 0);

	/* Create hbox for phase buttons/indicators */
	phase_box = gtk_hbox_new(TRUE, 0);

	/* Create separator between our area and phase indicator */
	h_sep = gtk_hseparator_new();

	/* Pack status area and separator */
	gtk_box_pack_start(GTK_BOX(left_vbox), phase_box, FALSE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(left_vbox), h_sep, FALSE, TRUE, 0);

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

	/* Create "em" tag for message buffer */
	gtk_text_buffer_create_tag(message_buffer, FORMAT_EM,
	                           "weight", "bold", NULL);

	/* Create "chat" tag for message buffer */
	gtk_text_buffer_create_tag(message_buffer, FORMAT_CHAT,
	                           "weight", "bold", NULL);

	/* Create "phase" tag for message buffer */
	gtk_text_buffer_create_tag(message_buffer, FORMAT_PHASE,
	                           "foreground", "#0000aa", NULL);

	/* Create "takeover" tag for message buffer */
	gtk_text_buffer_create_tag(message_buffer, FORMAT_TAKEOVER,
	                           "foreground", "#ff0000", NULL);

	/* Create "goal" tag for message buffer */
	gtk_text_buffer_create_tag(message_buffer, FORMAT_GOAL,
	                           "foreground", "#eeaa00", NULL);

	/* Create "prestige" tag for message buffer */
	gtk_text_buffer_create_tag(message_buffer, FORMAT_PRESTIGE,
	                           "foreground", "#8800bb", NULL);

	/* Create "verbose" tag for message buffer */
	gtk_text_buffer_create_tag(message_buffer, FORMAT_VERBOSE,
	                           "foreground", "#aaaaaa", NULL);

	/* Create "discard" tag for message buffer */
	gtk_text_buffer_create_tag(message_buffer, FORMAT_DRAW,
	                           "foreground", "#aaaaaa", NULL);

	/* Create "discard" tag for message buffer */
	gtk_text_buffer_create_tag(message_buffer, FORMAT_DISCARD,
	                           "foreground", "#aaaaaa", NULL);

	/* Create "debug" tag for message buffer */
	gtk_text_buffer_create_tag(message_buffer, FORMAT_DEBUG,
	                           "background", "#ff5555", NULL);

	/* Get iterator for end of buffer */
	gtk_text_buffer_get_end_iter(message_buffer, &end_iter);

	/* Get mark at end of buffer */
	message_end = gtk_text_buffer_create_mark(message_buffer, NULL,
	                                          &end_iter, FALSE);

	/* Connect "expose-event" */
	g_signal_connect_after(G_OBJECT(message_view), "expose-event",
	                       G_CALLBACK(message_view_expose), NULL);


	/* Connect "motion-notify-event" */
	g_signal_connect_after(G_OBJECT(message_view), "motion-notify-event",
	                       G_CALLBACK(message_motion), NULL);

	/* Enable motion event mask */
	gtk_widget_set_events(message_view, GDK_POINTER_MOTION_MASK |
	                                    GDK_POINTER_MOTION_HINT_MASK);

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

	/* Pack goal and active areas into table box */
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

	/* Also attach Shift+Enter */
	gtk_widget_add_accelerator(GTK_WIDGET(action_button), "clicked",
		window_accel, GDK_Return, GDK_SHIFT_MASK, 0);

	/* Set CAN_DEFAULT flag on action button */
	GTK_WIDGET_SET_FLAGS(action_button, GTK_CAN_DEFAULT);

	/* Set action button as default widget */
	gtk_window_set_default(GTK_WINDOW(window), action_button);

	/* Pack label and button into action box */
	gtk_box_pack_start(GTK_BOX(action_box), action_prompt, TRUE, TRUE, 0);
	gtk_box_pack_end(GTK_BOX(action_box), action_button, FALSE, TRUE, 0);

	/* Pack table area into right vbox */
	gtk_box_pack_start(GTK_BOX(right_vbox), table_box, TRUE, TRUE, 0);

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
	game_list = gtk_tree_store_new(14,
		G_TYPE_INT,    //  0: Game id
		G_TYPE_STRING, //  1: Description
		G_TYPE_STRING, //  2: Create name
		G_TYPE_INT,    //  3: Password?
		G_TYPE_STRING, //  4: Number of players
		G_TYPE_STRING, //  5: Expansion name
		G_TYPE_INT,    //  6: Advanced game?
		G_TYPE_INT,    //  7: Disable goals?
		G_TYPE_INT,    //  8: Disable takeovers?
		G_TYPE_INT,    //  9: Game speed
		G_TYPE_INT,    // 10: My game?
		G_TYPE_INT,    // 11: Checkboxes visible?
		G_TYPE_INT,    // 12: Min players
		G_TYPE_INT);   // 13: Max players

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
	create_button = gtk_button_new_with_mnemonic("C_reate Game");

	/* Connect "clicked" signal of create game button */
	g_signal_connect(G_OBJECT(create_button), "clicked",
	                 G_CALLBACK(create_dialog), NULL);

	/* Add handler for keypresses */
	gtk_widget_add_accelerator(create_button, "key-signal",
	                           window_accel, GDK_r, GDK_CONTROL_MASK, 0);

	/* Connect key-signal */
	g_signal_connect(G_OBJECT(create_button), "key-signal",
	                 G_CALLBACK(create_dialog), NULL);

	/* Create join button */
	join_button = gtk_button_new_with_mnemonic("_Join Game");

	/* Connect "clicked" signal of join game button */
	g_signal_connect(G_OBJECT(join_button), "clicked",
	                 G_CALLBACK(join_game), NULL);

	/* Add handler for keypresses */
	gtk_widget_add_accelerator(join_button, "key-signal",
	                           window_accel, GDK_j, GDK_CONTROL_MASK, 0);

	/* Connect key-signal */
	g_signal_connect(G_OBJECT(join_button), "key-signal",
	                 G_CALLBACK(join_game), NULL);

	/* Create leave button */
	leave_button = gtk_button_new_with_mnemonic("_Leave Game");

	/* Connect "clicked" signal of leave game button */
	g_signal_connect(G_OBJECT(leave_button), "clicked",
	                 G_CALLBACK(leave_game), NULL);

	/* Add handler for keypresses */
	gtk_widget_add_accelerator(leave_button, "key-signal",
	                           window_accel, GDK_l, GDK_CONTROL_MASK, 0);

	/* Connect key-signal */
	g_signal_connect(G_OBJECT(leave_button), "key-signal",
	                 G_CALLBACK(leave_game), NULL);

	/* Create kick player button */
	kick_button = gtk_button_new_with_mnemonic("_Kick Player");

	/* Connect "clicked" signal of kick player button */
	g_signal_connect(G_OBJECT(kick_button), "clicked",
	                 G_CALLBACK(kick_player), NULL);

	/* Add handler for keypresses */
	gtk_widget_add_accelerator(kick_button, "key-signal",
	                           window_accel, GDK_k, GDK_CONTROL_MASK, 0);

	/* Connect key-signal */
	g_signal_connect(G_OBJECT(kick_button), "key-signal",
	                 G_CALLBACK(kick_player), NULL);

	/* Create add AI player button */
	addai_button = gtk_button_new_with_mnemonic("Add A_I Player");

	/* Connect "clicked" signal of add AI button */
	g_signal_connect(G_OBJECT(addai_button), "clicked",
	                 G_CALLBACK(add_ai_player), NULL);

	/* Add handler for keypresses */
	gtk_widget_add_accelerator(addai_button, "key-signal",
	                           window_accel, GDK_i, GDK_CONTROL_MASK, 0);

	/* Connect key-signal */
	g_signal_connect(G_OBJECT(addai_button), "key-signal",
	                 G_CALLBACK(add_ai_player), NULL);

	/* Create start button */
	start_button = gtk_button_new_with_mnemonic("_Start Game");

	/* Connect "clicked" signal of start game button */
	g_signal_connect(G_OBJECT(start_button), "clicked",
	                 G_CALLBACK(start_game), NULL);

	/* Add handler for keypresses */
	gtk_widget_add_accelerator(start_button, "key-signal",
	                           window_accel, GDK_s, GDK_CONTROL_MASK, 0);

	/* Connect key-signal */
	g_signal_connect(G_OBJECT(start_button), "key-signal",
	                 G_CALLBACK(start_game), NULL);

	/* Create blank filler label */
	label = gtk_label_new("");

	/* Add buttons to join hbox */
	gtk_box_pack_start(GTK_BOX(join_hbox), label, TRUE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(join_hbox), create_button, FALSE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(join_hbox), join_button, FALSE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(join_hbox), leave_button, FALSE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(join_hbox), kick_button, FALSE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(join_hbox), addai_button, FALSE, TRUE, 0);
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
	user_list = gtk_list_store_new(2, G_TYPE_STRING, G_TYPE_INT);

	/* Create view for chat users */
	users_view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(user_list));

	/* Create text renderer */
	render = gtk_cell_renderer_text_new();

	/* Create toggle button renderer */
	toggle_render = gtk_cell_renderer_toggle_new();

	/* Create columns for user list */
	gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(users_view),
	                                            -1, "Users online", render,
	                                            "text", 0, NULL);
	gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(users_view),
	                                            -1, "In game",
	                                            toggle_render,
	                                            "active", 1, NULL);

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

	/* Create "chat" tag for usernames */
	gtk_text_buffer_create_tag(chat_buffer, FORMAT_CHAT, "weight", "bold", NULL);

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

	/* Simulate client state changed */
	gui_client_state_changed(FALSE, FALSE);

	/* Show all widgets */
	gtk_widget_show_all(window);

	/* Switch to main game view */
	switch_view(0, 0);

#ifdef __APPLE__
	/* Setup OS X style menus */
	GtkMacMenuGroup *group = gtk_mac_menu_add_app_menu_group();
	gtk_mac_menu_add_app_menu_item(group,
	                               GTK_MENU_ITEM(about_item), NULL);

	group = gtk_mac_menu_add_app_menu_group();
	gtk_mac_menu_add_app_menu_item(group,
	                               GTK_MENU_ITEM(option_item),
	                               "Preferences...");

	gtk_mac_menu_set_quit_menu_item(GTK_MENU_ITEM(quit_item));

	gtk_widget_hide(menu_bar);
	gtk_mac_menu_set_menu_bar(GTK_MENU_SHELL(menu_bar));
#endif

	/* Reset GUI */
	reset_gui();

	/* Modify GUI for current setup */
	modify_gui(TRUE);

	/* Check if loading from file */
	if (fname)
	{
		/* Try to load savefile into load state */
		if (load_game(&real_game, fname) < 0)
		{
			/* Format error */
			sprintf(msg, "Failed to load game from file %s\n", fname);

			/* Show error */
			display_error(msg);
		}
		else
		{
			/* Force current game over */
			real_game.game_over = 1;

			/* Loop over players */
			for (i = 0; i < real_game.num_players; ++i)
			{
				/* Remember log size */
				orig_log_size[i] = real_game.p[i].choice_size;
			}

			/* Switch to loaded state when able */
			restart_loop = RESTART_LOAD;
		}
	}

	/* Run games */
	run_game();

	/* Exit */
	return 0;
}
