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
#include "rftg.h"

/*
 * AI verbosity.
 */
int verbose = 0;

/*
 * Current (real) game state.
 */
static game real_game;

/*
 * Player we're playing as.
 */
static int player_us;

/*
 * User asked to start a new game.
 */
static int start_new;

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

	/* Card is eligible for being chosen */
	int eligible;

	/* Card is from tableau */
	int special;

	/* Card is selected */
	int selected;

	/* Card is not eligible for selection, but should be colored anyway */
	int color;

	/* Card is covered (by a good) */
	int covered;

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
 * Restriction types on action button sensitivity.
 */
#define RESTRICT_NUM      1
#define RESTRICT_PAY      2
#define RESTRICT_GOOD     3
#define RESTRICT_TAKEOVER 4
#define RESTRICT_DEFEND   5

/*
 * Restriction on action button.
 */
static int action_restrict;
static int action_min, action_max, action_payment_which;
static power *action_power;
static int action_choice1, action_choice2;

/*
 * Card images.
 */
static GdkPixbuf *image_cache[MAX_DESIGN];

/*
 * Goal card images.
 */
static GdkPixbuf *goal_cache[MAX_GOAL];

/*
 * Card back image.
 */
static GdkPixbuf *card_back;

/*
 * Widgets used in multiple functions.
 */
static GtkWidget *full_image;
static GtkWidget *hand_area, *our_area;
static GtkWidget *opp_area[MAX_PLAYER], *orig_area[MAX_PLAYER];
static GtkWidget *goal_area;
static GtkWidget *player_status[MAX_PLAYER], *orig_status[MAX_PLAYER];
static GtkWidget *player_frame[MAX_PLAYER];
static GtkWidget *game_status;
static GtkWidget *phase_labels[MAX_ACTION];
static GtkWidget *action_box, *action_prompt, *action_button;

/*
 * Text buffer for message area.
 */
static GtkWidget *message_view;

/*
 * Mark at end of message area text buffer.
 */
static GtkTextMark *message_end;

/*
 * Forward declarations.
 */
static void redraw_hand(void);
static void redraw_table(void);
static void redraw_status(void);
static void redraw_goal(void);
static void redraw_everything(void);

/*
 * Add text to the message buffer.
 */
void message_add(char *msg)
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
 * Clear message log.
 */
static void clear_log(void)
{
	GtkTextBuffer *message_buffer;

	/* Get message buffer */
	message_buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(message_view));

	/* Clear text buffer */
	gtk_text_buffer_set_text(message_buffer, "", 0);
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
	bundle = g_file_new_for_path("images.data");

	/* Open file for reading */
	fs = G_INPUT_STREAM(g_file_read(bundle, NULL, NULL));

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
	card_back = gdk_pixbuf_new_from_file("image/cardback.jpg", NULL);

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
			printf("Cannot open goal image %s!|n", fn);
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
	                        list, n, special, ns);
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
	return good_chosen(&sim, player_us, action_power, list, n);
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

	/* Check for no target */
	if (target == -1) return 0;

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

	/* Check for no special card */
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
 * Refresh the full-size card image.
 *
 * Called when the pointer moves over a small card image.
 */
static gboolean redraw_full(GtkWidget *widget, GdkEventCrossing *event,
                            gpointer data)
{
	design *d_ptr = (design *)data;

	/* Check for no design */
	if (!d_ptr)
	{
		/* Set image to card back */
		gtk_image_set_from_pixbuf(GTK_IMAGE(full_image), card_back);
	}
	else
	{
		/* Reset image */
		gtk_image_set_from_pixbuf(GTK_IMAGE(full_image),
		                          image_cache[d_ptr->index]);
	}

	/* Event handled */
	return TRUE;
}

/*
 * Create an event box containing the given card's image.
 */
static GtkWidget *new_image_box(design *d_ptr, int w, int h, int color,
                                int border)
{
	GdkPixbuf *buf, *border_buf, *blank_buf;
	GtkWidget *image, *box;
	int bw;

	/* Check for no image */
	if (!d_ptr)
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

	/* Redraw hand */
	redraw_hand();

	/* Redraw table */
	redraw_table();

	/* Event handled */
	return TRUE;
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
static void redraw_hand(void)
{
	GtkWidget *box;
	displayed *i_ptr;
	int count = 0, gap = 0, n;
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

		/* Check for "special" card from tableau */
		if (i_ptr->special)
		{
			/* Make extra space for gap */
			n++;

			/* Done */
			break;
		}
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

		/* Skip spot before first special card */
		if (i_ptr->special && !gap)
		{
			/* Increase count */
			count++;

			/* Remember gap is placed */
			gap = 1;
		}

		/* Get event box with image */
		box = new_image_box(i_ptr->d_ptr, card_w, card_h,
		                    i_ptr->eligible || i_ptr->color,
		                    i_ptr->special && i_ptr->selected);

		/* Place event box */
		gtk_fixed_put(GTK_FIXED(hand_area), box, count * width,
		              i_ptr->selected && !i_ptr->special ? 0 :
		                                             height - card_h);

		/* Check for eligible card */
		if (i_ptr->eligible)
		{
			/* Connect "button released" signal */
			g_signal_connect(G_OBJECT(box), "button-release-event",
			                 G_CALLBACK(card_selected), i_ptr);
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
		                    i_ptr->special && i_ptr->selected);

		/* Place event box */
		gtk_fixed_put(GTK_FIXED(area), box, x * width, y * height);

		/* Show image */
		gtk_widget_show_all(box);

		/* Check for good */
		if (i_ptr->covered || (i_ptr->selected && !i_ptr->special))
		{
			/* Get event box with no image */
			good_box = new_image_box(NULL, 3 * card_w / 4,
			                               3 * card_h / 4,
			                               i_ptr->eligible ||
			                                 i_ptr->color, 0);

			/* Place box on card */
			gtk_fixed_put(GTK_FIXED(area), good_box,
			              x * width + card_w / 4,
			              i_ptr->selected && !i_ptr->special ?
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
static void redraw_table(void)
{
	int i;

	/* First redraw our area */
	redraw_table_area(player_us, our_area);

	/* Loop over players */
	for (i = 0; i < real_game.num_players; i++)
	{
		/* Skip ourself */
		if (i == player_us) continue;

		/* Redraw opponent area */
		redraw_table_area(i, opp_area[i]);
	}
}

/*
 * Redraw the goal area.
 */
static void redraw_goal(void)
{
	player *p_ptr;
	GtkWidget *image;
	GdkPixbuf *buf;
	int i, j, n;
	int width, height, goal_h, y = 0;
	char msg[1024];

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
		if (i <= GOAL_FIRST_8_ACTIVE)
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

		/* Check for claimed goal */
		if (!real_game.goal_avail[i])
		{
			/* Create tooltip text */
			sprintf(msg, "Claimed by:");

			/* Loop over players */
			for (j = 0; j < real_game.num_players; j++)
			{
				/* Get player pointer */
				p_ptr = &real_game.p[j];

				/* Check for claimed */
				if (p_ptr->goal_claimed[i])
				{
					/* Add name to tooltip */
					strcat(msg, " ");
					strcat(msg, p_ptr->name);
				}
			}

			/* Set tooltip */
			gtk_widget_set_tooltip_text(image, msg);
		}

		/* Destroy local copy of the pixbuf */
		g_object_unref(G_OBJECT(buf));

		/* Place image */
		gtk_fixed_put(GTK_FIXED(goal_area), image, 0, y * height / 100);

		/* Show image */
		gtk_widget_show(image);

		/* Adjust distance to next card */
		if (i <= GOAL_FIRST_8_ACTIVE)
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
 * Action name abbreviations.
 */
static char *action_abbr[MAX_ACTION] =
{
	"E5",
	"E1",
	"D",
	"D",
	"S",
	"S",
	"CT",
	"C2",
	"P",
};

/*
 * Redraw a player's status information.
 */
static void redraw_status_area(int who, GtkWidget *label)
{
	char buf[1024];
	char text[1024];

	/* Clear label text */
	strcpy(text, "");

	/* Create card/VP text */
	sprintf(buf, "Active: %d  Hand: %d  VP: %d/%d/%d",
	        count_player_area(&real_game, who, WHERE_ACTIVE),
	        count_player_area(&real_game, who, WHERE_HAND),
	        real_game.p[who].vp, real_game.p[who].goal_vp,
		real_game.p[who].end_vp);

	/* Append card/VP text to label */
	strcat(text, buf);

	/* Check for actions chosen */
	if (real_game.cur_action >= ACT_EXPLORE_5_0)
	{
		/* Check for advanced game */
		if (real_game.advanced)
		{
			/* Show both actions */
			sprintf(buf, "  %s/%s",
			        action_abbr[real_game.p[who].action[0]],
			        action_abbr[real_game.p[who].action[1]]);
		}
		else
		{
			/* Show selected action */
			sprintf(buf, "  %s",
			        action_abbr[real_game.p[who].action[0]]);
		}

		/* Append action text to label */
		strcat(text, buf);
	}

	/* Set label */
	gtk_label_set_text(GTK_LABEL(label), text);
}

/*
 * Redraw all player's status information.
 */
static void redraw_status(void)
{
	card *c_ptr;
	char buf[1024];
	int draw = 0, discard = 0, pool;
	int i;

	/* Score game */
	score_game(&real_game);

	/* Loop over players */
	for (i = 0; i < real_game.num_players; i++)
	{
		/* Redraw player's status */
		redraw_status_area(i, player_status[i]);
	}

	/* Loop over cards */
	for (i = 0; i < real_game.deck_size; i++)
	{
		/* Get card pointer */
		c_ptr = &real_game.deck[i];

		/* Check for card in draw pile */
		if (c_ptr->where == WHERE_DECK) draw++;

		/* Check for card in discard pile */
		if (c_ptr->where == WHERE_DISCARD) discard++;
	}

	/* Get chips in VP pool */
	pool = real_game.vp_pool;

	/* Do not display negative numbers */
	if (pool < 0) pool = 0;

	/* Create status label */
	sprintf(buf, "Draw: %d  Discard: %d  Pool: %d", draw, discard, pool);

	/* Set label */
	gtk_label_set_text(GTK_LABEL(game_status), buf);
}

/*
 * Draw phase indicators.
 */
static void redraw_phase(void)
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
static void redraw_everything(void)
{
	/* Redraw hand, table, and status area */
	redraw_hand();
	redraw_table();
	redraw_goal();
	redraw_status();
	redraw_phase();
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
	card *c_ptr1, *c_ptr2;

	/* Get card pointers */
	c_ptr1 = &real_game.deck[i_ptr1->index];
	c_ptr2 = &real_game.deck[i_ptr2->index];

	/* Sort by order played */
	return c_ptr1->order - c_ptr2->order;
}

/*
 * Reset our list of cards in hand.
 */
static void reset_hand(int color)
{
	displayed *i_ptr;
	card *c_ptr;
	int i;

	/* Clear size */
	hand_size = 0;

	/* Loop over cards in deck */
	for (i = 0; i < real_game.deck_size; i++)
	{
		/* Get card pointer */
		c_ptr = &real_game.deck[i];

		/* Skip unowned cards */
		if (c_ptr->owner != player_us) continue;

		/* Skip cards not in hand */
		if (c_ptr->where != WHERE_HAND) continue;

		/* Get next entry in hand list */
		i_ptr = &hand[hand_size++];

		/* Add card information */
		i_ptr->index = i;
		i_ptr->d_ptr = c_ptr->d_ptr;

		/* Card is not eligible or selected */
		i_ptr->eligible = i_ptr->selected = 0;

		/* Card is not special */
		i_ptr->special = 0;

		/* Set color flag */
		i_ptr->color = color;
	}

	/* Sort hand */
	qsort(hand, hand_size, sizeof(displayed), cmp_hand);
}

/*
 * Reset list of displayed cards on the table for the given player.
 */
static void reset_table(int who, int color)
{
	displayed *i_ptr;
	card *c_ptr;
	int i;

	/* Clear size */
	table_size[who] = 0;

	/* Loop over cards in deck */
	for (i = 0; i < real_game.deck_size; i++)
	{
		/* Get card pointer */
		c_ptr = &real_game.deck[i];

		/* Skip unowned cards */
		if (c_ptr->owner != who) continue;

		/* Skip cards not on table */
		if (c_ptr->where != WHERE_ACTIVE) continue;

		/* Get next entry in table list */
		i_ptr = &table[who][table_size[who]++];

		/* Add card information */
		i_ptr->index = i;
		i_ptr->d_ptr = c_ptr->d_ptr;

		/* Card is not eligible or selected */
		i_ptr->eligible = i_ptr->selected = 0;

		/* Set color flag */
		i_ptr->color = color;

		/* Check for good */
		i_ptr->covered = (c_ptr->covered != -1);

		/* Card is not special */
		i_ptr->special = 0;
	}

	/* Sort list */
	qsort(table[who], table_size[who], sizeof(displayed), cmp_table);
}

/*
 * Reset our hand list, and all players' table lists.
 */
static void reset_cards(int color_hand, int color_table)
{
	int i;

	/* Reset hand */
	reset_hand(color_hand);

	/* Loop over players */
	for (i = 0; i < real_game.num_players; i++)
	{
		/* Reset table of player */
		reset_table(i, color_table);
	}
}

/*
 * Callback when action choice changes.
 */
static void action_choice_changed(GtkComboBox *combo, gpointer data)
{
	int which = GPOINTER_TO_INT(data);
	char *name;
	int i;

	/* Get name of choice */
	name = gtk_combo_box_get_active_text(combo);

	/* Loop over choices */
	for (i = 0; i < MAX_ACTION; i++)
	{
		/* Check for match */
		if (!strcmp(name, action_name[i]))
		{
			/* Store choice */
			if (which == 1)
			{
				/* Store first choice */
				action_choice1 = i;
			}
			else
			{
				/* Store second choice */
				action_choice2 = i;
			}

			/* Stop looking */
			break;
		}
	}

	/* Check for illegal combination */
	if (action_choice1 == action_choice2 &&
	    action_choice1 != ACT_DEVELOP && action_choice1 != ACT_SETTLE)
	{
		/* Disable action button */
		gtk_widget_set_sensitive(action_button, FALSE);
	}
	else
	{
		/* Enable action button */
		gtk_widget_set_sensitive(action_button, TRUE);
	}
}

/*
 * Choose two actions.
 */
static void gui_choose_action_advanced(game *g, int who, int action[2])
{
	GtkWidget *combo1, *combo2;
	int i;
	char *name;

	/* Deactivate action button */
	gtk_widget_set_sensitive(action_button, FALSE);

	/* Reset displayed cards */
	reset_cards(TRUE, TRUE);

	/* Redraw everything */
	redraw_everything();

	/* Set prompt */
	gtk_label_set_text(GTK_LABEL(action_prompt), "Choose Action");

	/* Create two combo boxes */
	combo1 = gtk_combo_box_new_text();
	combo2 = gtk_combo_box_new_text();

	/* Loop over actions */
	for (i = 0; i < MAX_ACTION; i++)
	{
		/* Skip second develop/settle */
		if (i == ACT_DEVELOP2 || i == ACT_SETTLE2) continue;

		/* Get action name */
		name = action_name[i];

		/* Append option to combo box */
		gtk_combo_box_append_text(GTK_COMBO_BOX(combo1), name);
		gtk_combo_box_append_text(GTK_COMBO_BOX(combo2), name);
	}

	/* Set first choice */
	gtk_combo_box_set_active(GTK_COMBO_BOX(combo1), 0);
	gtk_combo_box_set_active(GTK_COMBO_BOX(combo2), 0);

	/* Set cached action choices */
	action_choice1 = action_choice2 = 0;

	/* Add combo boxes to action box */
	gtk_box_pack_end(GTK_BOX(action_box), combo1, FALSE, TRUE, 0);
	gtk_box_pack_end(GTK_BOX(action_box), combo2, FALSE, TRUE, 0);

	/* Connect "changed" signal */
	g_signal_connect(G_OBJECT(combo1), "changed",
	                 G_CALLBACK(action_choice_changed), GINT_TO_POINTER(1));
	g_signal_connect(G_OBJECT(combo2), "changed",
	                 G_CALLBACK(action_choice_changed), GINT_TO_POINTER(2));

	/* Show everything */
	gtk_widget_show_all(combo1);
	gtk_widget_show_all(combo2);

	/* Process events */
	gtk_main();

	/* Destroy combo boxes */
	gtk_widget_destroy(combo1);
	gtk_widget_destroy(combo2);

	/* Set actions */
	action[0] = action_choice1;
	action[1] = action_choice2;

	/* Handle double Develop */
	if (action[0] == ACT_DEVELOP && action[1] == ACT_DEVELOP)
	{
		/* Set second choice to second develop */
		action[1] = ACT_DEVELOP2;
	}

	/* Handle double Settle */
	if (action[0] == ACT_SETTLE && action[1] == ACT_SETTLE)
	{
		/* Set second choice to second settle */
		action[1] = ACT_SETTLE2;
	}

	/* Reorder actions if needed */
	if (action[0] > action[1])
	{
		/* Swap actions */
		i = action[0];
		action[0] = action[1];
		action[1] = i;
	}
}

/*
 * Choose action card.
 */
void gui_choose_action(game *g, int who, int action[2])
{
	GtkWidget *combo;
	char *name;
	int i;

	/* Check for advanced game */
	if (real_game.advanced)
	{
		/* Call advanced function instead */
		return gui_choose_action_advanced(g, who, action);
	}

	/* Activate action button */
	gtk_widget_set_sensitive(action_button, TRUE);

	/* Reset displayed cards */
	reset_cards(TRUE, TRUE);

	/* Redraw everything */
	redraw_everything();

	/* Set prompt */
	gtk_label_set_text(GTK_LABEL(action_prompt), "Choose Action");

	/* Create simple combo box */
	combo = gtk_combo_box_new_text();

	/* Loop over actions */
	for (i = 0; i < MAX_ACTION; i++)
	{
		/* Skip second develop/settle */
		if (i == ACT_DEVELOP2 || i == ACT_SETTLE2) continue;

		/* Get action name */
		name = action_name[i];

		/* Append option to combo box */
		gtk_combo_box_append_text(GTK_COMBO_BOX(combo), name);
	}

	/* Set first choice */
	gtk_combo_box_set_active(GTK_COMBO_BOX(combo), 0);

	/* Add combo box to action box */
	gtk_box_pack_end(GTK_BOX(action_box), combo, FALSE, TRUE, 0);

	/* Show everything */
	gtk_widget_show_all(combo);

	/* Process events */
	gtk_main();

	/* Get selection name */
	name = gtk_combo_box_get_active_text(GTK_COMBO_BOX(combo));

	/* Loop over choices */
	for (i = 0; i < MAX_ACTION; i++)
	{
		/* Check for match */
		if (!strcmp(name, action_name[i]))
		{
			/* Stop looking */
			break;
		}
	}

	/* Destroy combo box */
	gtk_widget_destroy(combo);

	/* Set choice */
	action[0] = i;
	action[1] = -1;
}

/*
 * React to action choices.
 */
void gui_react_action(game *g, int who, int action[2])
{
}

/*
 * Choose a start world from those given.
 */
int gui_choose_start(game *g, int who, int list[], int num)
{
	char buf[1024];
	displayed *i_ptr;
	card *c_ptr;
	int i;

	/* Create prompt */
	sprintf(buf, "Choose start world");

	/* Set prompt */
	gtk_label_set_text(GTK_LABEL(action_prompt), buf);

	/* Set restrictions on action button */
	action_restrict = RESTRICT_NUM;
	action_min = action_max = 1;

	/* Deactivate action button */
	gtk_widget_set_sensitive(action_button, FALSE);

	/* Reset displayed cards */
	reset_cards(TRUE, FALSE);

	/* Add start worlds to table */
	for (i = 0; i < num; i++)
	{
		/* Get card pointer */
		c_ptr = &real_game.deck[list[i]];

		/* Get next entry in table list */
		i_ptr = &table[player_us][table_size[player_us]++];

		/* Add card information */
		i_ptr->index = list[i];
		i_ptr->d_ptr = c_ptr->d_ptr;

		/* Card is eligible */
		i_ptr->eligible = 1;
		i_ptr->special = 1;
		
		/* Card is not selected */
		i_ptr->selected = 0;

		/* Card is not covered */
		i_ptr->covered = 0;
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

		/* Skip un-selected cards */
		if (!i_ptr->selected) continue;

		/* Return card index */
		return i_ptr->index;
	}

	/* Error */
	return -1;
}

/*
 * Ask the player to discard some number of cards from the set given.
 */
void gui_choose_discard(game *g, int who, int list[], int num, int discard)
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
	reset_cards(FALSE, TRUE);

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

				/* Check for discarding most of cards */
				if (discard > num / 2)
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

	/* Discard chosen cards */
	discard_callback(&real_game, player_us, list, n);
}

/*
 * Choose a card to place for the Develop or Settle phases.
 */
int gui_choose_place(game *g, int who, int list[], int num, int phase)
{
	char buf[1024];
	displayed *i_ptr;
	int i, j;

	/* Create prompt */
	sprintf(buf, "Choose card to %s",
	        phase == PHASE_DEVELOP ? "develop" : "settle");

	/* Set prompt */
	gtk_label_set_text(GTK_LABEL(action_prompt), buf);

	/* Set restrictions on action button */
	action_restrict = RESTRICT_NUM;
	action_min = 0;
	action_max = 1;

	/* Activate action button */
	gtk_widget_set_sensitive(action_button, TRUE);

	/* Reset displayed cards */
	reset_cards(FALSE, TRUE);

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
void gui_choose_pay(game *g, int who, int which, int list[], int num,
                    int special[], int num_special)
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
	reset_cards(FALSE, FALSE);

	/* Set button restriction */
	action_restrict = RESTRICT_PAY;
	action_payment_which = which;

	/* Deactivate action button */
	gtk_widget_set_sensitive(action_button, action_check_payment());

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

	/* Loop over special cards */
	for (i = 0; i < num_special; i++)
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

				/* Card is special */
				i_ptr->special = 1;
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

	/* Discard chosen cards */
	payment_callback(&real_game, who, which, list, n, special, ns);
}

/*
 * Choose a world to attempt a takeover of.
 *
 * We must also choose a card showing a takeover power to use.
 */
int gui_choose_takeover(game *g, int who, int list[], int num,
                        int special[], int num_special)
{
	displayed *i_ptr;
	char buf[1024];
	int i, j, k;

	/* Create prompt */
	sprintf(buf, "Choose world to takeover and power to use");

	/* Set prompt */
	gtk_label_set_text(GTK_LABEL(action_prompt), buf);

	/* Reset displayed cards */
	reset_cards(FALSE, FALSE);

	/* Set button restriction */
	action_restrict = RESTRICT_TAKEOVER;

	/* Deactivate action button */
	gtk_widget_set_sensitive(action_button, FALSE);

	/* Loop over cards in list */
	for (i = 0; i < num; i++)
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
					i_ptr->special = 1;
				}
			}
		}
	}

	/* Loop over special cards */
	for (i = 0; i < num_special; i++)
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

				/* Card is special */
				i_ptr->special = 1;
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
				/* Add to list */
				list[0] = i_ptr->index;
			}
		}
	}

	/* Return chosen world */
	return list[0];
}

/*
 * Choose a method to defend against a takeover.
 */
void gui_choose_defend(game *g, int who, int which, int opponent, int deficit,
                       int list[], int num, int special[], int num_special)
{
	card *c_ptr;
	displayed *i_ptr;
	char buf[1024];
	int i, j, n = 0, ns = 0;

	/* Get card we are defending */
	c_ptr = &real_game.deck[which];

	/* Create prompt */
	sprintf(buf, "Choose defense for %s (need %d extra military)",
	        c_ptr->d_ptr->name, deficit);

	/* Set prompt */
	gtk_label_set_text(GTK_LABEL(action_prompt), buf);

	/* Reset displayed cards */
	reset_cards(FALSE, TRUE);

	/* Set button restriction */
	action_restrict = RESTRICT_DEFEND;

	/* Deactivate action button */
	gtk_widget_set_sensitive(action_button, TRUE);

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

	/* Loop over special cards */
	for (i = 0; i < num_special; i++)
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

				/* Card is special */
				i_ptr->special = 1;
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

	/* Defend with chosen cards */
	defend_callback(&real_game, who, deficit, list, n, special, ns);
}

/*
 * Choose a good to trade.
 */
void gui_choose_trade(game *g, int who, int list[], int num, int no_bonus)
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
	reset_cards(TRUE, FALSE);

	/* Loop over cards in list */
	for (i = 0; i < num; i++)
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
			/* Trade this good */
			trade_chosen(&real_game, player_us, i_ptr->index,
			             no_bonus);

			/* Done */
			break;
		}
	}
}

/*
 * Ask user which consume power to use.
 */
void gui_choose_consume(game *g, int who, power olist[], int num)
{
	GtkWidget *combo;
	power *o_ptr;
	char buf[1024], *name, buf2[1024];
	int i;

	/* Activate action button */
	gtk_widget_set_sensitive(action_button, TRUE);

	/* Reset displayed cards */
	reset_cards(TRUE, TRUE);

	/* Redraw everything */
	redraw_everything();

	/* Set prompt */
	gtk_label_set_text(GTK_LABEL(action_prompt), "Choose Consume power");

	/* Create simple combo box */
	combo = gtk_combo_box_new_text();

	/* Loop over powers */
	for (i = 0; i < num; i++)
	{
		/* Get power pointer */
		o_ptr = &olist[i];

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
			if (o_ptr->code & P4_CONSUME_ANY)
			{
				/* Any good */
				name = "";
			}
			else if (o_ptr->code & P4_CONSUME_NOVELTY)
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

			/* Check for two goods */
			if (o_ptr->code & P4_CONSUME_TWO)
			{
				/* Start string */
				sprintf(buf, "Consume two %sgoods for ", name);
			}
			else if (o_ptr->code & P4_DISCARD_HAND)
			{
				/* Make string */
				sprintf(buf, "Consume from hand for ");
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

				/* Check for points as well */
				if (o_ptr->code & P4_GET_VP)
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

				/* Check for points as well */
				if (o_ptr->code & P4_GET_VP)
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

	/* Use chosen power */
	consume_chosen(&real_game, who, &olist[i]);
}

/*
 * Consume cards from hand.
 */
void gui_choose_consume_hand(game *g, int who, power *o_ptr, int list[],
                             int num)
{
	char buf[1024];
	displayed *i_ptr;
	int i, j, n = 0;

	/* Create prompt */
	sprintf(buf, "Choose up to %d cards to consume", o_ptr->times);

	/* Set prompt */
	gtk_label_set_text(GTK_LABEL(action_prompt), buf);

	/* Set restrictions on action button */
	action_restrict = RESTRICT_NUM;
	action_min = 0;
	action_max = o_ptr->times;

	/* Activate action button */
	gtk_widget_set_sensitive(action_button, TRUE);

	/* Reset displayed cards */
	reset_cards(FALSE, TRUE);

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
			/* Add to list */
			list[n++] = i_ptr->index;
		}
	}

	/* Discard chosen cards */
	consume_hand_chosen(&real_game, player_us, o_ptr, list, n);
}

/*
 * Choose good(s) to consume.
 */
void gui_choose_good(game *g, int who, power *o_ptr, int goods[], int num,
                     int min, int max)
{
	char buf[1024];
	displayed *i_ptr;
	int i, j, n = 0;

	/* Create prompt */
	sprintf(buf, "Choose goods to consume");

	/* Set prompt */
	gtk_label_set_text(GTK_LABEL(action_prompt), buf);

	/* Set restrictions on action button */
	action_restrict = RESTRICT_GOOD;
	action_min = min;
	action_max = max;
	action_power = o_ptr;

	/* Deactivate action button */
	gtk_widget_set_sensitive(action_button, action_check_number());

	/* Reset displayed cards */
	reset_cards(TRUE, FALSE);

	/* Loop over cards in list */
	for (i = 0; i < num; i++)
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

	/* Consume goods */
	good_chosen(&real_game, player_us, o_ptr, goods, n);
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
	reset_cards(TRUE, TRUE);

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
	reset_cards(FALSE, TRUE);

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
	reset_cards(FALSE, TRUE);

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

		/* Card is eligible */
		i_ptr->eligible = 1;
		i_ptr->special = 1;
		
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
void gui_choose_windfall(game *g, int who, int list[], int num)
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
	reset_cards(TRUE, FALSE);

	/* Loop over cards in list */
	for (i = 0; i < num; i++)
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
			/* Produce on world */
			produce_world(&real_game, who, i_ptr->index);

			/* Done */
			break;
		}
	}
}

/*
 * Choose a produce power to use.
 */
void gui_choose_produce(game *g, int who, power olist[], int num)
{
	GtkWidget *combo;
	power *o_ptr;
	char buf[1024];
	int i;

	/* Activate action button */
	gtk_widget_set_sensitive(action_button, TRUE);

	/* Reset displayed cards */
	reset_cards(TRUE, TRUE);

	/* Redraw everything */
	redraw_everything();

	/* Set prompt */
	gtk_label_set_text(GTK_LABEL(action_prompt), "Choose Produce power");

	/* Create simple combo box */
	combo = gtk_combo_box_new_text();

	/* Loop over powers */
	for (i = 0; i < num; i++)
	{
		/* Get power pointer */
		o_ptr = &olist[i];

		/* Check for simple powers */
		if (o_ptr->code & P5_DISCARD_PRODUCE)
		{
			/* Make string */
			sprintf(buf, "Discard to produce");
		}
		else if (o_ptr->code & P5_DRAW_EACH_NOVELTY)
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

	/* Use chosen power */
	produce_chosen(&real_game, who, &olist[i]);
}

/*
 * Discard a card in order to produce.
 */
void gui_choose_discard_produce(game *g, int who, int world, int list[],
                                int num)
{
	char buf[1024];
	displayed *i_ptr;
	int i, j;

	/* Create prompt */
	sprintf(buf, "Choose discard to produce on %s",
	        real_game.deck[world].d_ptr->name);

	/* Set prompt */
	gtk_label_set_text(GTK_LABEL(action_prompt), buf);

	/* Set restrictions on action button */
	action_restrict = RESTRICT_NUM;
	action_min = 0;
	action_max = 1;

	/* Activate action button */
	gtk_widget_set_sensitive(action_button, TRUE);

	/* Reset displayed cards */
	reset_cards(FALSE, TRUE);

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
			/* Discard chosen card */
			discard_produce_chosen(&real_game, player_us, world,
			                       i_ptr->index);
		}
	}
}

/*
 * Player spots have been rotated.
 */
static void gui_notify_rotation(game *g)
{
	GtkWidget *temp_area, *temp_status;
	int i;

	/* Remember our new player index */
	player_us--;

	/* Handle wraparound */
	if (player_us < 0) player_us = real_game.num_players - 1;

	/* Save first opponent area */
	temp_area = opp_area[0];
	temp_status = player_status[0];

	/* Rotate opponent areas */
	for (i = 0; i < real_game.num_players - 1; i++)
	{
		/* Copy area and status */
		opp_area[i] = opp_area[i + 1];
		player_status[i] = player_status[i + 1];
	}

	/* Move first area to last spot */
	opp_area[i] = temp_area;
	player_status[i] = temp_status;
}

/*
 * Interface to GUI decision functions.
 */
static interface gui_func =
{
	NULL,
	gui_notify_rotation,
	gui_choose_start,
	gui_choose_action,
	gui_react_action,
	gui_choose_discard,
	gui_choose_place,
	gui_choose_pay,
	gui_choose_takeover,
	gui_choose_defend,
	gui_choose_trade,
	gui_choose_consume,
	gui_choose_consume_hand,
	gui_choose_good,
	gui_choose_lucky,
	gui_choose_ante,
	gui_choose_keep,
	gui_choose_windfall,
	gui_choose_produce,
	gui_choose_discard_produce,
	NULL,
	NULL
};

/*
 * Reset player structures.
 */
static void reset_game(void)
{
	int i;

	/* Reset our player index */
	player_us = 0;

	/* Restore opponent areas to originial */
	for (i = 0; i < MAX_PLAYER; i++)
	{
		/* Restore table area */
		opp_area[i] = orig_area[i];

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
 * Run games forever.
 */
static void run_game(void)
{
	char buf[1024];
	int i;

	/* Loop forever */
	while (1)
	{
		/* Reset game */
		reset_game();

		/* Initialize game */
		init_game(&real_game);

		/* Play game rounds until finished */
		while (game_round(&real_game));

		/* Check for new game request */
		if (start_new)
		{
			/* Clear flag */
			start_new = 0;

			/* Clear text log */
			clear_log();

			/* Start again */
			continue;
		}

		/* Declare winner */
		declare_winner(&real_game);

		/* Loop over players */
		for (i = 0; i < real_game.num_players; i++)
		{
			/* Skip non-winner */
			if (!real_game.p[i].winner) continue;

			/* Format message */
			sprintf(buf, "%s wins with %d.\n", real_game.p[i].name,
			                                 real_game.p[i].end_vp);

			/* Add message */
			message_add(buf);
		}

		/* Reset displayed cards */
		reset_cards(TRUE, TRUE);

		/* Redraw everything */
		redraw_everything();

		/* Create prompt */
		sprintf(buf, "Game Over");

		/* Set prompt */
		gtk_label_set_text(GTK_LABEL(action_prompt), buf);

		/* Process events */
		gtk_main();

		/* Clear text log */
		clear_log();

		/* Clear new game flag */
		start_new = 0;
	}
}

/*
 * Modify GUI elements for the correct number of players.
 */
static void modify_gui(void)
{
	int i;

	/* Check for basic game */
	if (!real_game.expanded)
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
		gtk_widget_show(player_frame[i]);

		/* Show opponent area */
		if (orig_area[i]) gtk_widget_show(orig_area[i]);
	}

	/* Loop over non-existant players */
	for ( ; i < MAX_PLAYER; i++)
	{
		/* Hide status */
		gtk_widget_hide(player_frame[i]);

		/* Hide opponent area */
		if (orig_area[i]) gtk_widget_hide(orig_area[i]);
	}

	/* Handle pending events */
	while (gtk_events_pending()) gtk_main_iteration();
}

/*
 * New game.
 */
static void new_game(GtkMenuItem *menu_item, gpointer data)
{
	/* Force game over */
	real_game.game_over = 1;
	
	/* Set new game flag */
	start_new = 1;

	/* Quit waiting for events */
	gtk_main_quit();
}

/*
 * Expansion level names.
 */
static char *exp_names[] =
{
	"Base game only",
	"The Gathering Storm",
	"Rebel vs Imperium",
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
 * Widget for two-player advanced game.
 */
static GtkWidget *advanced_check;

/*
 * Current selections for next game expansion level and number of players.
 */
static int next_exp, next_player;

/*
 * React to an expansion level button being toggled.
 */
static void exp_toggle(GtkToggleButton *button, gpointer data)
{
	int i = GPOINTER_TO_INT(data);

	/* Check for button set */
	if (gtk_toggle_button_get_active(button)) next_exp = i;
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
 * Select parameters and start a new game.
 */
static void select_parameters(GtkMenuItem *menu_item, gpointer data)
{
	GtkWidget *dialog;
	GtkWidget *radio = NULL;
	GtkWidget *exp_box, *player_box;
	GtkWidget *exp_frame, *player_frame;
	int i;

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
	                             real_game.advanced);

	/* Add checkbox to dialog box */
	gtk_container_add(GTK_CONTAINER(GTK_DIALOG(dialog)->vbox),
	                  advanced_check);

	/* Disable advanced checkbox if not two-player game */
	if (real_game.num_players != 2)
	{
		/* Disable checkbox */
		gtk_widget_set_sensitive(advanced_check, FALSE);
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
		real_game.expanded = next_exp;

		/* Set number of players */
		real_game.num_players = next_player;

		/* Set advanced game flag */
		real_game.advanced = (next_player == 2) &&
		                gtk_toggle_button_get_active(
		                             GTK_TOGGLE_BUTTON(advanced_check));

		/* Recreate GUI elements for new number of players */
		modify_gui();

		/* Force game over */
		real_game.game_over = 1;

		/* Start new game */
		start_new = 1;

		/* Quit waiting for events */
		gtk_main_quit();
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
		i_ptr->eligible = i_ptr->special = 0;
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
		i_ptr->eligible = i_ptr->special = 0;
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
 * Show AI debugging information.
 */
static void debug_ai_dialog(GtkMenuItem *menu_item, gpointer data)
{
	GtkWidget *dialog;
	GtkWidget *label, *table;
	double role[MAX_PLAYER][MAX_ACTION];
	double win_prob[MAX_PLAYER][MAX_PLAYER];
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
	ai_debug(&real_game, role, win_prob);

	/* Create label */
	label = gtk_label_new("Role choice probabilities:");

	/* Pack label */
	gtk_container_add(GTK_CONTAINER(GTK_DIALOG(dialog)->vbox), label);

	/* Create table for role probabilities */
	table = gtk_table_new(real_game.num_players + 1, MAX_ACTION + 1, FALSE);

	/* Set spacings between columns */
	gtk_table_set_col_spacings(GTK_TABLE(table), 5);

	/* Loop over action names */
	for (i = 0; i < MAX_ACTION; i++)
	{
		/* Create label */
		label = gtk_label_new(action_name[i]);

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
		for (j = 0; j < MAX_ACTION; j++)
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

	/* Show everything */
	gtk_widget_show_all(dialog);

	/* Run dialog */
	gtk_dialog_run(GTK_DIALOG(dialog));

	/* Destroy dialog box */
	gtk_widget_destroy(dialog);
}


/*
 * Quit.
 */
static void quit_game(GtkMenuItem *menu_item, gpointer data)
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
 * Setup windows, callbacks, etc, then let GTK take over.
 */
int main(int argc, char *argv[])
{
	GtkWidget *window;
	GtkWidget *main_vbox, *main_hbox;
	GtkWidget *left_vbox, *right_vbox;
	GtkWidget *menu_bar, *game_menu, *debug_menu, *help_menu;
	GtkWidget *game_item, *debug_item, *help_item;
	GtkWidget *new_item, *select_item, *quit_item;
	GtkWidget *debug_card_item, *debug_ai_item, *about_item;
	GtkWidget *h_sep, *v_sep;
	GtkWidget *msg_scroll;
	GtkWidget *table_box, *active_box, *opp_box, *area;
	GtkWidget *phase_box, *label;
	GtkTextIter end_iter;
	GtkTextBuffer *message_buffer;
	GdkColor color;
	int i, num_players = 3, expansion = 0, advanced = 0;
	unsigned int seed;

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

	/* Parse arguments */
	for (i = 1; i < argc; i++)
	{
		/* Check for number of players */
		if (!strcmp(argv[i], "-p"))
		{
			/* Set number of players */
			num_players = atoi(argv[++i]);
		}

		/* Check for expansion level */
		else if (!strcmp(argv[i], "-e"))
		{
			/* Set expansion level */
			expansion = atoi(argv[++i]);
		}

		/* Check for advanced game */
		else if (!strcmp(argv[i], "-a"))
		{
			/* Set advanced */
			advanced = 1;
		}

		/* Check for random seed */
		else if (!strcmp(argv[i], "-r"))
		{
			/* Set random seed */
			real_game.random_seed = atoi(argv[++i]);
		}
	}

	/* Set number of players */
	real_game.num_players = num_players;

	/* Set expansion level */
	real_game.expanded = expansion;

	/* Set advanced flag */
	real_game.advanced = advanced;

	/* Copy random seed */
	seed = real_game.random_seed;

	/* Reset game's player structures */
	reset_game();

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

	/* Create main vbox to hold menu bar, then rest of game area */
	main_vbox = gtk_vbox_new(FALSE, 0);

	/* Create menu bar */
	menu_bar = gtk_menu_bar_new();

	/* Create menu item for 'game' menu */
	game_item = gtk_menu_item_new_with_label("Game");

	/* Add game item to menu bar */
	gtk_menu_shell_append(GTK_MENU_SHELL(menu_bar), game_item);

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
	select_item = gtk_menu_item_new_with_label("Select parameters...");
	quit_item = gtk_menu_item_new_with_label("Quit"); 

	/* Add items to game menu */
	gtk_menu_shell_append(GTK_MENU_SHELL(game_menu), new_item);
	gtk_menu_shell_append(GTK_MENU_SHELL(game_menu), select_item);
	gtk_menu_shell_append(GTK_MENU_SHELL(game_menu), quit_item);

	/* Create debug menu */
	debug_menu = gtk_menu_new();

	/* Create debug menu items */
	debug_card_item = gtk_menu_item_new_with_label("Debug cards...");
	debug_ai_item = gtk_menu_item_new_with_label("Debug AI...");

	/* Add items to debug menu */
	gtk_menu_shell_append(GTK_MENU_SHELL(debug_menu), debug_card_item);
	gtk_menu_shell_append(GTK_MENU_SHELL(debug_menu), debug_ai_item);

	/* Create help menu */
	help_menu = gtk_menu_new();

	/* Create about menu item */
	about_item = gtk_menu_item_new_with_label("About...");

	/* Add item to help menu */
	gtk_menu_shell_append(GTK_MENU_SHELL(help_menu), about_item);

	/* Attach events to menu items */
	g_signal_connect(G_OBJECT(new_item), "activate",
	                 G_CALLBACK(new_game), NULL);
	g_signal_connect(G_OBJECT(select_item), "activate",
	                 G_CALLBACK(select_parameters), NULL);
	g_signal_connect(G_OBJECT(quit_item), "activate",
	                 G_CALLBACK(quit_game), NULL);
	g_signal_connect(G_OBJECT(debug_card_item), "activate",
	                 G_CALLBACK(debug_card_dialog), NULL);
	g_signal_connect(G_OBJECT(debug_ai_item), "activate",
	                 G_CALLBACK(debug_ai_dialog), NULL);
	g_signal_connect(G_OBJECT(about_item), "activate",
	                 G_CALLBACK(about_dialog), NULL);

	/* Set submenus */
	gtk_menu_item_set_submenu(GTK_MENU_ITEM(game_item), game_menu);
	gtk_menu_item_set_submenu(GTK_MENU_ITEM(debug_item), debug_menu);
	gtk_menu_item_set_submenu(GTK_MENU_ITEM(help_item), help_menu);

	/* Create main hbox to contain status box and game area box */
	main_hbox = gtk_hbox_new(FALSE, 0);

	/* Create left vbox for status information */
	left_vbox = gtk_vbox_new(FALSE, 0);

	/* Create "card view" image */
	full_image = gtk_image_new_from_pixbuf(card_back);

	/* Create separator for status info */
	h_sep = gtk_hseparator_new();

	/* Pack image and separator into left vbox */
	gtk_box_pack_start(GTK_BOX(left_vbox), full_image, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(left_vbox), h_sep, FALSE, FALSE, 0);

	/* Create game status label */
	game_status = gtk_label_new("");

	/* Add status to status vbox */
	gtk_box_pack_start(GTK_BOX(left_vbox), game_status, FALSE, FALSE, 0);

	/* Start with opponent on left */
	i = (player_us + 1) % MAX_PLAYER;

	/* Loop over players */
	while (1)
	{
		/* Create status frame */
		player_frame[i] = gtk_frame_new(real_game.p[i].name);

		/* Create status label */
		player_status[i] = gtk_label_new("");
		orig_status[i] = player_status[i];

		/* Add status to frame */
		gtk_container_add(GTK_CONTAINER(player_frame[i]),
		                  player_status[i]);

		/* Pack frame into status vbox */
		gtk_box_pack_start(GTK_BOX(left_vbox), player_frame[i], FALSE,
		                   FALSE, 0);

		/* Check for last player (us) */
		if (i == player_us) break;

		/* Advance to next player */
		i = (i + 1) % MAX_PLAYER;
	}

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
	opp_box = gtk_hbox_new(FALSE, 0);

	/* Start with left opponent */
	i = (player_us + 1) % MAX_PLAYER;

	/* Loop over opponents */
	while (1)
	{
		/* Create area for opponent */
		area = gtk_fixed_new();

		/* Save area pointer */
		opp_area[i] = area;
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

		/* Pack area into opponent box */
		gtk_box_pack_start(GTK_BOX(opp_box), area, TRUE, TRUE, 0);

		/* Next player */
		i = (i + 1) % MAX_PLAYER;

		/* Stop at ourself */
		if (i == player_us) break;

#if 0
		/* Make vertical separator */
		v_sep = gtk_vseparator_new();

		/* Add to opponent box */
		gtk_box_pack_start(GTK_BOX(opp_box), v_sep, FALSE, TRUE, 0);
#endif
	}

	/* Create area for our played cards */
	our_area = gtk_fixed_new();

	/* Give widget its own window */
	gtk_fixed_set_has_window(GTK_FIXED(our_area), TRUE);

	/* Lookup our color */
	gdk_color_parse(player_colors[player_us], &color);

	/* Set area's background color */
	gtk_widget_modify_bg(our_area, GTK_STATE_NORMAL, &color);

	/* Have area negotiate new size when needed */
	g_signal_connect(G_OBJECT(our_area), "size-request",
			 G_CALLBACK(table_request), NULL);

	/* Pack active card vbox */
	gtk_box_pack_start(GTK_BOX(active_box), opp_box, TRUE, TRUE, 0);

	/* Create separator between opponent and our area */
	h_sep = gtk_hseparator_new();

	/* Pack separator and our table area */
	gtk_box_pack_start(GTK_BOX(active_box), h_sep, FALSE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(active_box), our_area, TRUE, TRUE, 0);

	/* Create seperator between goal area and active area */
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
	for (i = 0; i < MAX_ACTION; i++)
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

	/* Create action prompt */
	action_prompt = gtk_label_new("Action");

	/* Create action button */
	action_button = gtk_button_new_with_label("Done");

	/* Attach event */
	g_signal_connect(G_OBJECT(action_button), "clicked",
	                 G_CALLBACK(action_pressed), NULL);

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

	/* Pack menu and main hbox into main vbox */
	gtk_box_pack_start(GTK_BOX(main_vbox), menu_bar, FALSE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(main_vbox), main_hbox, TRUE, TRUE, 0);

	/* Add main hbox to main window */
	gtk_container_add(GTK_CONTAINER(window), main_vbox);

	/* Show all widgets */
	gtk_widget_show_all(window);

	/* Modify GUI for current setup */
	modify_gui();

	/* Run games */
	run_game();

	/* Exit */
	return 0;
}
