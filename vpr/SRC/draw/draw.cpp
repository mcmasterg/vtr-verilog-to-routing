/*********************************** Top-level Summary *************************************
 This is VPR's main graphics application program. The program interacts with graphics.c, 
 which provides an API for displaying graphics on both X11 and Win32. The most important
 subroutine in this file is drawscreen(), which is a callback function that X11 or Win32
 will call whenever the screen needs to be updated. Then, drawscreen() will decide what 
 drawing subroutines to call depending on whether PLACEMENT or ROUTING is shown on screen 
 and whether any of the menu buttons has been triggered. As a note, looks into draw_global.c
 for understanding the data structures associated with drawing.
 
 Authors: Vaughn Betz, Long Yu (Mike) Wang
 Last updated: August 2013
 */

#include <cstdio>
#include <cfloat>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <sstream>
#include <array>
using namespace std;

#include "vtr_assert.h"
#include "vtr_matrix.h"
#include "vtr_log.h"
#include "vtr_color_map.h"

#include "vpr_utils.h"
#include "vpr_error.h"

#include "globals.h"
#include "graphics.h"
#include "path_delay.h"
#include "draw.h"
#include "read_xml_arch_file.h"
#include "draw_global.h"
#include "intra_logic_block.h"
#include "atom_netlist.h"
#include "tatum/report/TimingPathCollector.hpp"

#ifdef WIN32 /* For runtime tracking in WIN32. The clock() function defined in time.h will *
			  * track CPU runtime.														   */
#include <time.h>
#else /* For X11. The clock() function in time.h will not output correct time difference   *
	   * for X11, because the graphics is processed by the Xserver rather than local CPU,  *
	   * which means tracking CPU time will not be the same as the actual wall clock time. *
	   * Thus, so use gettimeofday() in sys/time.h to track actual calendar time.          */
#include <sys/time.h>
#endif

#include "rr_graph.h"

/****************************** Define Macros *******************************/

#define DEFAULT_RR_NODE_COLOR BLACK
//#define TIME_DRAWSCREEN /* Enable if want to track runtime for drawscreen() */

//The arrow head position for turning/straight-thru connections in a switch box
constexpr float SB_EDGE_TURN_ARROW_POSITION = 0.2;
constexpr float SB_EDGE_STRAIGHT_ARROW_POSITION = 0.9;

//Kelly's maximum contrast colors (Kenneth Kelly, "Twenty-Two Colors of Maximum Contrast", Color Eng. 3(6), 1943)
const std::array<t_color,21> kelly_max_contrast_colors = {
    //t_color(242, 243, 244), //white: skip white since it doesn't contrast well with the light background
    t_color( 34,  34,  34), //black
    t_color(243, 195,   0), //yellow
    t_color(135,  86, 146), //purple
    t_color(243, 132,   0), //orange
    t_color(161, 202, 241), //light blue
    t_color(190,   0,  50), //red
    t_color(194, 178, 128), //buf
    t_color(132, 132, 130), //gray
    t_color(  0, 136,  86), //green
    t_color(230, 143, 172), //purplish pink
    t_color(  0, 103, 165), //blue
    t_color(249, 147, 121), //yellowish pink
    t_color( 96,  78, 151), //violet
    t_color(246, 166,   0), //orange yellow
    t_color(179,  68, 108), //purplish red
    t_color(220, 211,   0), //greenish yellow
    t_color(136,  45,  23), //redish brown
    t_color(141, 182,   0), //yellow green
    t_color(101,  69,  34), //yellowish brown
    t_color(226,  88,  34), //reddish orange
    t_color( 43,  61,  38)  //olive green
};

/************************** File Scope Variables ****************************/

std::string rr_highlight_message;

/********************** Subroutines local to this module ********************/

static void toggle_nets(void (*drawscreen)(void));
static void toggle_rr(void (*drawscreen)(void));
static void toggle_congestion(void (*drawscreen)(void));
static void toggle_crit_path(void (*drawscreen_ptr)(void));

static void drawscreen(void);
static void redraw_screen(void);
static void drawplace(void);
static void drawnets(void);
static void drawroute(enum e_draw_net_type draw_net_type);
static void draw_congestion(void);
static void draw_crit_path();

static void highlight_blocks(float x, float y, t_event_buttonPressed button_info);
static void act_on_mouse_over(float x, float y);
static void deselect_all(void);

void draw_partial_route(const std::vector<int>& rr_nodes_to_draw);
static void draw_rr(void);
static void draw_rr_edges(int from_node);
static void draw_rr_pin(int inode, const t_color& color);
static void draw_rr_chanx(int inode, const t_color& color);
static void draw_rr_chany(int inode, const t_color& color);
static t_bound_box draw_get_rr_chan_bbox(int inode);
static void draw_pin_to_chan_edge(int pin_node, int chan_node);
static void draw_x(float x, float y, float size);
static void draw_pin_to_pin(int opin, int ipin);
static void draw_rr_switch(float from_x, float from_y, float to_x, float to_y,
						   bool buffered);
static void draw_chany_to_chany_edge(int from_node, int to_node,
									 int to_track, short switch_type);
static void draw_chanx_to_chanx_edge(int from_node, int to_node,
									 int to_track, short switch_type);
static void draw_chanx_to_chany_edge(int chanx_node, int chanx_track, int chany_node, 
									 int chany_track, enum e_edge_dir edge_dir,
									 short switch_type);
static int get_track_num(int inode, int **chanx_track, int **chany_track);
static bool draw_if_net_highlighted (int inet);
static void draw_highlight_fan_in_fan_out(int hit_node);
static void highlight_nets(char *message, int hit_node);
static int draw_check_rr_node_hit (float click_x, float click_y);
static void highlight_rr_nodes(float x, float y);
static void draw_highlight_blocks_color(t_type_ptr type, int bnum);
static void draw_reset_blk_color(int i);

static inline bool LOD_screen_area_test_square(float width, float screen_area_threshold);
static inline bool default_triangle_LOD_screen_area_test();
static inline bool triangle_LOD_screen_area_test(float arrow_size);

static inline void draw_mux_with_size(t_point origin, e_side orientation, float height, int size);
static inline t_bound_box draw_mux(t_point origin, e_side orientation, float height);
static inline t_bound_box draw_mux(t_point origin, e_side orientation, float height, float width, float height_scale);

static void draw_flyline_timing_edge(t_point start, t_point end, float incr_delay);
static void draw_routed_timing_edge(tatum::NodeId start_tnode, tatum::NodeId end_tnode, float incr_delay, t_color color);
static void draw_routed_timing_edge_connection(tatum::NodeId src_tnode, tatum::NodeId sink_tnode, t_color color);

static std::vector<int> trace_routed_connection_rr_nodes(const t_net_pin* driver_clb_net_pin, const t_net_pin* sink_clb_net_pin);
static bool trace_routed_connection_rr_nodes_recurr(const t_rt_node* rt_node, int sink_rr_node, std::vector<int>& rr_nodes_on_path);
static short find_switch(int prev_inode, int inode);

t_color to_t_color(vtr::Color<float> color);

/********************** Subroutine definitions ******************************/


void init_graphics_state(bool show_graphics_val, int gr_automode_val,
		enum e_route_type route_type) 
{	
	/* Call accessor functions to retrieve global variables. */
	t_draw_state* draw_state = get_draw_state_vars();

	/* Sets the static show_graphics and gr_automode variables to the    *
	 * desired values.  They control if graphics are enabled and, if so, *
	 * how often the user is prompted for input.                         */

	draw_state->show_graphics = show_graphics_val;
	draw_state->gr_automode = gr_automode_val;
	draw_state->draw_route_type = route_type;
}

void update_screen(ScreenUpdatePriority priority, const char *msg, enum pic_type pic_on_screen_val,
		std::shared_ptr<SetupTimingInfo> setup_timing_info) {

	/* Updates the screen if the user has requested graphics.  The priority  *
	 * value controls whether or not the Proceed button must be clicked to   *
	 * continue.  Saves the pic_on_screen_val to allow pan and zoom redraws. */

	t_draw_state* draw_state = get_draw_state_vars();

	if (!draw_state->show_graphics) /* Graphics turned off */
		return;

	/* If it's the type of picture displayed has changed, set up the proper  *
	 * buttons.                                                              */
	if (draw_state->pic_on_screen != pic_on_screen_val) {
		if (pic_on_screen_val == PLACEMENT && draw_state->pic_on_screen == NO_PICTURE) {
			create_button("Window", "Toggle Nets", toggle_nets);
			create_button("Toggle Nets", "Blk Internal", toggle_blk_internal);
            if(setup_timing_info) {
                create_button("Blk Internal", "Crit. Path", toggle_crit_path);
            }
		} else if (pic_on_screen_val == ROUTING && draw_state->pic_on_screen == PLACEMENT) {
			create_button("Blk Internal", "Toggle RR", toggle_rr);
			create_button("Toggle RR", "Congestion", toggle_congestion);
		} else if (pic_on_screen_val == PLACEMENT && draw_state->pic_on_screen == ROUTING) {
			destroy_button("Toggle RR");
			destroy_button("Congestion");
            if(setup_timing_info) {
                destroy_button("Crit. Path");
            }
		} else if (pic_on_screen_val == ROUTING
				&& draw_state->pic_on_screen == NO_PICTURE) {
			create_button("Window", "Toggle Nets", toggle_nets);
			create_button("Toggle Nets", "Blk Internal", toggle_blk_internal);
			create_button("Blk Internal", "Toggle RR", toggle_rr);
			create_button("Toggle RR", "Congestion", toggle_congestion);
            if(setup_timing_info) {
                create_button("Congestion", "Crit. Path", toggle_crit_path);
            }
		}
	}
	/* Save the main message. */

	vtr::strncpy(draw_state->default_message, msg, vtr::BUFSIZE);

    draw_state->setup_timing_info = setup_timing_info;

	draw_state->pic_on_screen = pic_on_screen_val;
	update_message(msg);
	drawscreen();
	if (int(priority) >= draw_state->gr_automode) {
        set_mouse_move_input(true); //Enable act_on_mouse_over callback
		event_loop(highlight_blocks, act_on_mouse_over, NULL, drawscreen);
	} else {
		flushinput();
	}
}

static void drawscreen() {

#ifdef TIME_DRAWSCREEN
	/* This can be used to test how long it takes for the redrawing routing to finish   *
	 * updating the screen for a given input which would cause the screen to be redrawn.*/

#ifdef WIN32
	clock_t drawscreen_begin,drawscreen_end;
	drawscreen_begin = clock();

#else /* For X11. The clock() function in time.h does not output correct time difference *
       * in Linux, so use gettimeofday() in sys/time.h for accurate runtime tracking. */
	struct timeval begin;
	gettimeofday(&begin,NULL);  /* get start time */

	unsigned long begin_time;
	begin_time = begin.tv_sec * 1000000 + begin.tv_usec;
#endif
#endif

	/* This is the screen redrawing routine that event_loop assumes exists.  *
	 * It erases whatever is on screen, then calls redraw_screen to redraw   *
	 * it.                                                                   */

    set_drawing_buffer(OFF_SCREEN);

	clearscreen();
	redraw_screen();

    copy_off_screen_buffer_to_screen();

#ifdef TIME_DRAWSCREEN

#ifdef WIN32
	drawscreen_end = clock();

    vtr::printf_info("Drawscreen took %f seconds.\n", (float)(drawscreen_end - drawscreen_begin) / CLOCKS_PER_SEC);

#else /* X11 */
	struct timeval end;
	gettimeofday(&end,NULL);  /* get end time */

	unsigned long end_time;
	end_time = end.tv_sec * 1000000 + end.tv_usec;

	unsigned long time_diff_microsec;
	time_diff_microsec = end_time - begin_time;

	vtr::printf_info("Drawscreen took %ld microseconds\n", time_diff_microsec);
#endif /* WIN32 */
#endif /* TIME_DRAWSCREEN */
}

static void redraw_screen() {

	/* The screen redrawing routine called by drawscreen and           *
	 * highlight_blocks.  Call this routine instead of drawscreen if   *
	 * you know you don't need to erase the current graphics, and want *
	 * to avoid a screen "flash".                                      */

	t_draw_state* draw_state = get_draw_state_vars();

	setfontsize(14); 

	drawplace();

	if (draw_state->show_blk_internal) {
		draw_internal_draw_subblk();
	}

	if (draw_state->pic_on_screen == PLACEMENT) {
		switch (draw_state->show_nets) {
			case DRAW_NETS:
				drawnets();
			break;
			case DRAW_LOGICAL_CONNECTIONS:
			break;
			default:
			break;
		}

        draw_crit_path();
	} else { /* ROUTING on screen */

		switch (draw_state->show_nets) {
			case DRAW_NETS:
				drawroute(ALL_NETS);
			break;
			case DRAW_LOGICAL_CONNECTIONS:
			// fall through
			default:
				draw_rr();
			break;
		}

        draw_crit_path();

		if (draw_state->show_congestion != DRAW_NO_CONGEST) {
			draw_congestion();
		}
	}

	draw_logical_connections();
}

static void toggle_nets(void (*drawscreen_ptr)(void)) {

	/* Enables/disables drawing of nets when a the user clicks on a button.    *
	 * Also disables drawing of routing resources.  See graphics.c for details *
	 * of how buttons work.                                                    */
	t_draw_state* draw_state = get_draw_state_vars();

	enum e_draw_nets new_state;

	switch (draw_state->show_nets) {
		case DRAW_NO_NETS:
			new_state = DRAW_NETS;
		break;
		case DRAW_NETS:
			new_state = DRAW_LOGICAL_CONNECTIONS;
		break;
		default:
		case DRAW_LOGICAL_CONNECTIONS:
			new_state = DRAW_NO_NETS;
		break;
	}

	draw_state->reset_nets_congestion_and_rr();
	draw_state->show_nets = new_state;

	update_message(draw_state->default_message);
	drawscreen_ptr();
}

static void toggle_rr(void (*drawscreen_ptr)(void)) {

	/* Cycles through the options for viewing the routing resources available   *
	 * in an FPGA.  If a routing isn't on screen, the routing graph hasn't been *
	 * built, and this routine doesn't switch the view. Otherwise, this routine *
	 * switches to the routing resource view.  Clicking on the toggle cycles    *
	 * through the options:  DRAW_NO_RR, DRAW_ALL_RR, DRAW_ALL_BUT_BUFFERS_RR,  *
	 * DRAW_NODES_AND_SBOX_RR, and DRAW_NODES_RR.                               */

	t_draw_state* draw_state = get_draw_state_vars();

	enum e_draw_rr_toggle new_state = (enum e_draw_rr_toggle) (((int)draw_state->draw_rr_toggle + 1) 
														  % ((int)DRAW_RR_TOGGLE_MAX));
	draw_state->reset_nets_congestion_and_rr();
	draw_state->draw_rr_toggle = new_state;

	update_message(draw_state->default_message);
	drawscreen_ptr();
}

static void toggle_congestion(void (*drawscreen_ptr)(void)) {

	/* Turns the congestion display on and off.   */
	t_draw_state* draw_state = get_draw_state_vars();

	char msg[vtr::BUFSIZE];
	int inode, num_congested;

	e_draw_congestion new_state = (enum e_draw_congestion) (((int)draw_state->show_congestion + 1) 
														  % ((int)DRAW_CONGEST_MAX));
	draw_state->reset_nets_congestion_and_rr();
	draw_state->show_congestion = new_state;

	if (draw_state->show_congestion == DRAW_NO_CONGEST) {
		update_message(draw_state->default_message);
	} else {
		num_congested = 0;
		for (inode = 0; inode < num_rr_nodes; inode++) {
			if (rr_node[inode].get_occ() > rr_node[inode].get_capacity()) {
				num_congested++;
			}
		}

		sprintf(msg, "%d routing resources are overused.", num_congested);
		update_message(msg);
	}

	drawscreen_ptr();
}

void toggle_blk_internal(void (*drawscreen_ptr)(void)) {
	t_draw_state *draw_state;

	/* Call accessor function to retrieve global variables. */
	draw_state = get_draw_state_vars();

	/* Increment the depth of sub-blocks to be shown */
	draw_state->show_blk_internal++;
	/* If depth exceeds maximum sub-block level in pb_graph, then
	 * disable internals drawing
	 */
	if (draw_state->show_blk_internal > draw_state->max_sub_blk_lvl)
		draw_state->show_blk_internal = 0;

	drawscreen_ptr();
}

static void toggle_crit_path(void (*drawscreen_ptr)(void)) {
	t_draw_state* draw_state = get_draw_state_vars();

    if (draw_state->pic_on_screen == PLACEMENT) {
        switch (draw_state->show_crit_path) {
            case DRAW_NO_CRIT_PATH:
                draw_state->show_crit_path = DRAW_CRIT_PATH_FLYLINES;
                break;
            case DRAW_CRIT_PATH_FLYLINES:
                draw_state->show_crit_path = DRAW_CRIT_PATH_FLYLINES_DELAYS;
                break;
            default:
                draw_state->show_crit_path = DRAW_NO_CRIT_PATH;
                break;
        };
    } else {
        VTR_ASSERT(draw_state->pic_on_screen == ROUTING);

        switch (draw_state->show_crit_path) {
            case DRAW_NO_CRIT_PATH:
                draw_state->show_crit_path = DRAW_CRIT_PATH_FLYLINES;
                break;
            case DRAW_CRIT_PATH_FLYLINES:
                draw_state->show_crit_path = DRAW_CRIT_PATH_FLYLINES_DELAYS;
                break;
            case DRAW_CRIT_PATH_FLYLINES_DELAYS:
                draw_state->show_crit_path = DRAW_CRIT_PATH_ROUTING;
                break;
            case DRAW_CRIT_PATH_ROUTING:
                draw_state->show_crit_path = DRAW_CRIT_PATH_ROUTING_DELAYS;
                break;
            default:
                draw_state->show_crit_path = DRAW_NO_CRIT_PATH;
                break;
        };
    }

	drawscreen_ptr();
}


void alloc_draw_structs(void) {
	/* Call accessor functions to retrieve global variables. */
	t_draw_coords* draw_coords = get_draw_coords_vars();
	t_draw_state* draw_state = get_draw_state_vars();

	/* Allocate the structures needed to draw the placement and routing.  Set *
	 * up the default colors for blocks and nets.                             */

	draw_coords->tile_x = (float *) vtr::malloc((nx + 2) * sizeof(float));
	draw_coords->tile_y = (float *) vtr::malloc((ny + 2) * sizeof(float));

	/* For sub-block drawings inside clbs */
	draw_internal_alloc_blk();

	draw_state->net_color = (t_color *) vtr::malloc(g_clbs_nlist.net.size() * sizeof(t_color));

	draw_state->block_color = (t_color *) vtr::malloc(num_blocks * sizeof(t_color));

	/* Space is allocated for draw_rr_node but not initialized because we do *
	 * not yet know information about the routing resources.				  */
	draw_state->draw_rr_node = (t_draw_rr_node *) vtr::malloc(
									num_rr_nodes * sizeof(t_draw_rr_node));

	deselect_all(); /* Set initial colors */
}

void free_draw_structs(void) {

	/* Free everything allocated by alloc_draw_structs. Called after close_graphics() *
	 * in vpr_api.c.
	 *
	 * For safety, set all the array pointers to NULL in case any data
	 * structure gets freed twice.													 */
	t_draw_state* draw_state = get_draw_state_vars();
	t_draw_coords* draw_coords = get_draw_coords_vars();

	if(draw_coords != NULL) {
		free(draw_coords->tile_x);  
		draw_coords->tile_x = NULL;
		free(draw_coords->tile_y);  
		draw_coords->tile_y = NULL;		
	}

	if(draw_state != NULL) {
		free(draw_state->net_color);  	
		draw_state->net_color = NULL;
		free(draw_state->block_color);  
		draw_state->block_color = NULL;

		free(draw_state->draw_rr_node);	
		draw_state->draw_rr_node = NULL;
	}
}

void init_draw_coords(float width_val) {

	/* Load the arrays containing the left and bottom coordinates of the clbs   *
	 * forming the FPGA.  tile_width_val sets the width and height of a drawn    *
	 * clb.                                                                     */
	t_draw_state* draw_state = get_draw_state_vars();
	t_draw_coords* draw_coords = get_draw_coords_vars();

	int i;
	int j;

	if (!draw_state->show_graphics)
		return; /* Graphics disabled */

	/* Each time routing is on screen, need to reallocate the color of each *
	 * rr_node, as the number of rr_nodes may change.						*/
	if (num_rr_nodes != 0) {
		draw_state->draw_rr_node = (t_draw_rr_node *) vtr::realloc(draw_state->draw_rr_node,
										(num_rr_nodes) * sizeof(t_draw_rr_node));
		for (i = 0; i < num_rr_nodes; i++) {
			draw_state->draw_rr_node[i].color = DEFAULT_RR_NODE_COLOR;
			draw_state->draw_rr_node[i].node_highlighted = false;
		}
	}

	draw_coords->tile_width = width_val;
	draw_coords->pin_size = 0.3;
	for (i = 0; i < num_types; ++i) {
		if (type_descriptors[i].num_pins > 0) {
			draw_coords->pin_size = min(draw_coords->pin_size,
					(draw_coords->get_tile_width() / (4.0F * type_descriptors[i].num_pins)));
		}
	}

	j = 0;
	for (i = 0; i < (nx + 1); i++) {
		draw_coords->tile_x[i] = (i * draw_coords->get_tile_width()) + j;
		j += chan_width.y_list[i] + 1; /* N wires need N+1 units of space */
	}
	draw_coords->tile_x[nx + 1] = ((nx + 1) * draw_coords->get_tile_width()) + j;

	j = 0;
	for (i = 0; i < (ny + 1); ++i) {
		draw_coords->tile_y[i] = (i * draw_coords->get_tile_width()) + j;
		j += chan_width.x_list[i] + 1;
	}
	draw_coords->tile_y[ny + 1] = ((ny + 1) * draw_coords->get_tile_width()) + j;

	/* Load coordinates of sub-blocks inside the clbs */
	draw_internal_init_blk();

	set_visible_world(
		0.0, 0.0,
		draw_coords->tile_y[ny + 1] + draw_coords->get_tile_width(), 
		draw_coords->tile_x[nx + 1] + draw_coords->get_tile_width()
	);
}

static void drawplace(void) {

	/* Draws the blocks placed on the proper clbs.  Occupied blocks are darker colours *
	 * while empty ones are lighter colours and have a dashed border.      */
	t_draw_state* draw_state = get_draw_state_vars();
	t_draw_coords* draw_coords = get_draw_coords_vars();

	int i, j, k, bnum;
	int num_sub_tiles;
	int height;

	setlinewidth(0);

	for (i = 0; i <= (nx + 1); i++) {
		for (j = 0; j <= (ny + 1); j++) {
			/* Only the first block of a group should control drawing */
			if (grid[i][j].width_offset > 0 || grid[i][j].height_offset > 0) 
				continue;


			num_sub_tiles = grid[i][j].type->capacity;
			/* Don't draw if tile capacity is zero. eg. corners. */
			if (num_sub_tiles == 0) {
				continue;
			}
			height = grid[i][j].type->height;

			for (k = 0; k < num_sub_tiles; ++k) {
				/* Graphics will look unusual for multiple height and capacity */
				VTR_ASSERT(height == 1 || num_sub_tiles == 1);

				/* Get coords of current sub_tile */
				t_bound_box abs_clb_bbox = draw_coords->get_absolute_clb_bbox(i,j,k);

				/* Look at the tile at start of large block */
				bnum = grid[i][j].blocks[k];

				/* Fill background for the clb. Do not fill if "show_blk_internal" 
				 * is toggled. 
				 */
				if (bnum != EMPTY_BLOCK && bnum != INVALID_BLOCK) {
					setcolor(draw_state->block_color[bnum]);
					fillrect(abs_clb_bbox);
				} else {
					/* colour empty blocks a particular colour depending on type  */
					if (grid[i][j].type->index < 3) {
						setcolor(WHITE);
					} else if (grid[i][j].type->index < 3 + MAX_BLOCK_COLOURS) {
						setcolor(BISQUE + grid[i][j].type->index - 3);
					} else {
						setcolor(BISQUE + MAX_BLOCK_COLOURS - 1);
					}
					fillrect(abs_clb_bbox);
				}

				setcolor(BLACK);

				setlinestyle((EMPTY_BLOCK == bnum) ? DASHED : SOLID);
				drawrect(abs_clb_bbox);

				/* Draw text if the space has parts of the netlist */
				if (bnum != EMPTY_BLOCK && bnum != INVALID_BLOCK) {
					float saved_rotation = gettextrotation();
					if (j == 0 || j == ny + 1) {
						settextrotation(90);
					}
					drawtext_in(abs_clb_bbox, block[bnum].name);
					if (j == 0 || j == ny + 1) {
						settextrotation(saved_rotation);
					}
				}

				/* Draw text for block type so that user knows what block */
				if (grid[i][j].width_offset == 0 && grid[i][j].height_offset == 0) {
					if (i > 0 && i <= nx && j > 0 && j <= ny) {
						drawtext(abs_clb_bbox.get_center() - t_point(0, abs_clb_bbox.get_width()/4),
								grid[i][j].type->name, abs_clb_bbox);
					}
				}
			}
		}
	}
}

static void drawnets(void) {
	t_draw_state* draw_state = get_draw_state_vars();
	t_draw_coords* draw_coords = get_draw_coords_vars();
	/* This routine draws the nets on the placement.  The nets have not *
	 * yet been routed, so we just draw a chain showing a possible path *
	 * for each net.  This gives some idea of future congestion.        */

	unsigned ipin, inet;
	int b1, b2;

	setlinestyle(SOLID);
	setlinewidth(0);

	/* Draw the net as a star from the source to each sink. Draw from centers of *
	 * blocks (or sub blocks in the case of IOs).                                */

	for (inet = 0; inet < g_clbs_nlist.net.size(); inet++) {
		if (g_clbs_nlist.net[inet].is_global)
			continue; /* Don't draw global nets. */

		setcolor(draw_state->net_color[inet]);
		b1 = g_clbs_nlist.net[inet].pins[0].block; /* The DRIVER */
		t_point driver_center = draw_coords->get_absolute_clb_bbox(block[b1]).get_center();

		for (ipin = 1; ipin < g_clbs_nlist.net[inet].pins.size(); ipin++) {
			b2 = g_clbs_nlist.net[inet].pins[ipin].block;
			t_point sink_center = draw_coords->get_absolute_clb_bbox(block[b2]).get_center();
			drawline(driver_center, sink_center);

			/* Uncomment to draw a chain instead of a star. */
			/* driver_center = sink_center;  */
		}
	}
}

static void draw_congestion(void) {

	/* Draws all the overused routing resources (i.e. congestion) in RED.   */
	t_draw_state* draw_state = get_draw_state_vars();

	int inode;

	setlinewidth(2);


    float min_congestion_ratio = 1.;
    float max_congestion_ratio = min_congestion_ratio;
	for (inode = 0; inode < num_rr_nodes; inode++) {
		short occ = rr_node[inode].get_occ();
        short capacity = rr_node[inode].get_capacity();

        float congestion_ratio = float(occ) / capacity;

        max_congestion_ratio = std::max(max_congestion_ratio, congestion_ratio);
    }

    char msg[vtr::BUFSIZE];
    sprintf(msg, "Overuse ratio range (%.2f, %.2f]", min_congestion_ratio, max_congestion_ratio);
    update_message(msg);

    vtr::PlasmaColorMap cmap(min_congestion_ratio, max_congestion_ratio);

	for (inode = 0; inode < num_rr_nodes; inode++) {
		short occ = rr_node[inode].get_occ();
        short capacity = rr_node[inode].get_capacity();

        float congestion_ratio = float(occ) / capacity;
		if (congestion_ratio > 1.) {


            t_color congested_color = to_t_color(cmap.color(congestion_ratio));

			switch (rr_node[inode].type) {
			case CHANX:
				if (draw_state->show_congestion == DRAW_CONGESTED &&
					occ > rr_node[inode].get_capacity()) {
					draw_rr_chanx(inode, congested_color);
				}
				else if (draw_state->show_congestion == DRAW_CONGESTED_AND_USED) {
					if (occ > rr_node[inode].get_capacity())
						draw_rr_chanx(inode, congested_color);
					else
						draw_rr_chanx(inode, BLUE);
				}
				break;

			case CHANY:
				if (draw_state->show_congestion == DRAW_CONGESTED &&
					occ > rr_node[inode].get_capacity()) {
					draw_rr_chany(inode, congested_color);
				}
				else if (draw_state->show_congestion == DRAW_CONGESTED_AND_USED) {
					if (occ > rr_node[inode].get_capacity())
						draw_rr_chany(inode, congested_color);
					else
						draw_rr_chany(inode, BLUE);
				}
				break;

			case IPIN:
			case OPIN:
				if (draw_state->show_congestion == DRAW_CONGESTED &&
					occ > rr_node[inode].get_capacity()) {
					draw_rr_pin(inode, congested_color);
				}
				else if (draw_state->show_congestion == DRAW_CONGESTED_AND_USED) {
					if (occ > rr_node[inode].get_capacity())
						draw_rr_pin(inode, congested_color);
					else
						draw_rr_pin(inode, BLUE);
				}
				break;
			default:
				break;
			}
		}
	}
}


void draw_rr(void) {

	/* Draws the routing resources that exist in the FPGA, if the user wants *
	 * them drawn.                                                           */
	t_draw_state* draw_state = get_draw_state_vars();

	int inode;

	if (draw_state->draw_rr_toggle == DRAW_NO_RR) {
		setlinewidth(3);
		drawroute(HIGHLIGHTED);
		setlinewidth(0);
		return;
	}

	setlinestyle(SOLID);

	for (inode = 0; inode < num_rr_nodes; inode++) {
		if (!draw_state->draw_rr_node[inode].node_highlighted) 
		{
			/* If not highlighted node, assign color based on type. */
			switch (rr_node[inode].type) {
			case CHANX:
			case CHANY:
				draw_state->draw_rr_node[inode].color = DEFAULT_RR_NODE_COLOR;
				break;
			case OPIN:
				draw_state->draw_rr_node[inode].color = PINK;
				break;
			case IPIN:
				draw_state->draw_rr_node[inode].color = LIGHTSKYBLUE;
				break;
			default:
				break;
			}
		}

		/* Now call drawing routines to draw the node. */
		switch (rr_node[inode].type) {

		case SOURCE:
		case SINK:
			break; /* Don't draw. */

		case CHANX:
			draw_rr_chanx(inode, draw_state->draw_rr_node[inode].color);
			draw_rr_edges(inode);
			break;

		case CHANY:
			draw_rr_chany(inode, draw_state->draw_rr_node[inode].color);
			draw_rr_edges(inode);
			break;

		case IPIN:
			draw_rr_pin(inode, draw_state->draw_rr_node[inode].color);
			break;

		case OPIN:
			draw_rr_pin(inode, draw_state->draw_rr_node[inode].color);
			draw_rr_edges(inode);
			break;

		default:
			vpr_throw(VPR_ERROR_OTHER, __FILE__, __LINE__, 
					"in draw_rr: Unexpected rr_node type: %d.\n", rr_node[inode].type);
		}
	}

	drawroute(HIGHLIGHTED);
}


static void draw_rr_chanx(int inode, const t_color& color) {

	/* Draws an x-directed channel segment.                       */
	t_draw_coords* draw_coords = get_draw_coords_vars();

	t_bound_box bound_box;
	int k; 

	// For CHANX, bound_box.bottom() is same as bound_box.top()
	bound_box = draw_get_rr_chan_bbox(inode);

	setcolor(color);
	if (color != DEFAULT_RR_NODE_COLOR) {
		// If wire is highlighted, then draw with thicker linewidth.
		setlinewidth(3);
		drawline(bound_box.bottom_left(), bound_box.top_right());
		setlinewidth(0);
	}
	else
		drawline(bound_box.bottom_left(), bound_box.top_right());

	// draw the arrows and small lines iff zoomed in really far.
	if (default_triangle_LOD_screen_area_test() == false) {
		return;
	}

	if (rr_node[inode].get_direction() == INC_DIRECTION) {
		/* Draw mux at start of wire */
        draw_mux_with_size(bound_box.bottom_left(), RIGHT, WIRE_DRAWING_WIDTH, rr_node[inode].get_fan_in());

		setcolor(BLACK);
		setlinewidth(0);
		draw_triangle_along_line(bound_box.right() - 0.15, bound_box.top(), bound_box.left(), 
								 bound_box.right(), bound_box.bottom(), bound_box.top());

		setcolor(LIGHTGREY);
		/* TODO: this looks odd, why does it ignore final block? does this mean nothing 
		 * appears with L=1 ? 
		 */
		for (k = rr_node[inode].get_xlow(); k < rr_node[inode].get_xhigh(); k++) {
			bound_box.right() = draw_coords->tile_x[k] + draw_coords->get_tile_width();
			draw_triangle_along_line(bound_box.right() - 0.15, bound_box.top(), bound_box.left(), 
									 bound_box.right(), bound_box.bottom(), bound_box.top());
			bound_box.right() = draw_coords->tile_x[k + 1];
			draw_triangle_along_line(bound_box.right() + 0.15, bound_box.top(), bound_box.left(), 
									 bound_box.right(), bound_box.bottom(), bound_box.top());
		}
		setcolor(color);
	} else if (rr_node[inode].get_direction() == DEC_DIRECTION) {
		/* Draw mux at start of wire */
        draw_mux_with_size(bound_box.top_right(), LEFT, WIRE_DRAWING_WIDTH, rr_node[inode].get_fan_in());

		setlinewidth(0);
		draw_triangle_along_line(bound_box.left() + 0.15, bound_box.bottom(), bound_box.right(), 
								 bound_box.left(), bound_box.top(), bound_box.bottom());
		setcolor(LIGHTGREY);
		for (k = rr_node[inode].get_xhigh(); k > rr_node[inode].get_xlow(); k--) {
			bound_box.left() = draw_coords->tile_x[k];
			draw_triangle_along_line(bound_box.left() + 0.15, bound_box.bottom(), bound_box.right(), 
									 bound_box.left(), bound_box.top(), bound_box.bottom());
			bound_box.left() = draw_coords->tile_x[k - 1] + draw_coords->get_tile_width();
			draw_triangle_along_line(bound_box.left() - 0.15, bound_box.bottom(), bound_box.right(), 
									 bound_box.left(), bound_box.top(), bound_box.bottom());
		}
		setcolor(color);
	}
}

static void draw_rr_chany(int inode, const t_color& color) {

	/* Draws a y-directed channel segment.                       */
	t_draw_coords* draw_coords = get_draw_coords_vars();

	t_bound_box bound_box;
	int k; 

	// Get the coordinates of the channel wire segment.
	// For CHANY, bound_box.left() is equal to bound_box.right().
	bound_box = draw_get_rr_chan_bbox(inode);

	setcolor(color);
	if (color != DEFAULT_RR_NODE_COLOR) 
	{
		// If wire is highlighted, then draw with thicker linewidth.
		setlinewidth(3);

		drawline(bound_box.bottom_left(), bound_box.top_right());
		
		setlinewidth(0);
	}
	else
	{
		drawline(bound_box.bottom_left(), bound_box.top_right());

	}

	// draw the arrows and small lines iff zoomed in really far.
	if (default_triangle_LOD_screen_area_test() == false) {
		return;
	}
	
	if (rr_node[inode].get_direction() == INC_DIRECTION) {
		/* Draw mux at start of wire */
        draw_mux_with_size(bound_box.bottom_left(), TOP, WIRE_DRAWING_WIDTH, rr_node[inode].get_fan_in());

		setcolor(BLACK);
		setlinewidth(0);
		draw_triangle_along_line(bound_box.right(), bound_box.top() - 0.15, bound_box.left(), 
								 bound_box.right(), bound_box.bottom(), bound_box.top());
		setcolor(LIGHTGREY);
		for (k = rr_node[inode].get_ylow(); k < rr_node[inode].get_yhigh(); k++) {
			bound_box.top() = draw_coords->tile_y[k] + draw_coords->get_tile_width();
			draw_triangle_along_line(bound_box.right(), bound_box.top() - 0.15, bound_box.left(), 
									 bound_box.right(), bound_box.bottom(), bound_box.top());
			bound_box.top() = draw_coords->tile_y[k + 1];
			draw_triangle_along_line(bound_box.right(), bound_box.top() + 0.15, bound_box.left(), 
									 bound_box.right(), bound_box.bottom(), bound_box.top());
		}
		setcolor(color);
	} else if (rr_node[inode].get_direction() == DEC_DIRECTION) {
		/* Draw mux at start of wire */
        draw_mux_with_size(bound_box.top_right(), BOTTOM, WIRE_DRAWING_WIDTH, rr_node[inode].get_fan_in());

		setcolor(BLACK);
		setlinewidth(0);
		draw_triangle_along_line(bound_box.left(), bound_box.bottom() + 0.15, bound_box.right(), 
								 bound_box.left(), bound_box.top(), bound_box.bottom());
		setcolor(LIGHTGREY);
		for (k = rr_node[inode].get_yhigh(); k > rr_node[inode].get_ylow(); k--) {
			bound_box.bottom() = draw_coords->tile_y[k];
			draw_triangle_along_line(bound_box.left(), bound_box.bottom() + 0.15, bound_box.right(), 
									 bound_box.left(), bound_box.top(), bound_box.bottom());
			bound_box.bottom() = draw_coords->tile_y[k - 1] + draw_coords->get_tile_width();
			draw_triangle_along_line(bound_box.left(), bound_box.bottom() - 0.15, bound_box.right(), 
									 bound_box.left(), bound_box.top(), bound_box.bottom());
		}
		setcolor(color);
	}
}

static void draw_rr_edges(int inode) {

	/* Draws all the edges that the user wants shown between inode and what it *
	 * connects to.  inode is assumed to be a CHANX, CHANY, or IPIN.           */
	t_draw_state* draw_state = get_draw_state_vars();

	t_rr_type from_type, to_type;
	int to_node, from_ptc_num, to_ptc_num;
	short switch_type;

	from_type = rr_node[inode].type;

	if ((draw_state->draw_rr_toggle == DRAW_NODES_RR)
			|| (draw_state->draw_rr_toggle == DRAW_NODES_AND_SBOX_RR && from_type == OPIN)) {
		return; /* Nothing to draw. */
	}

	from_ptc_num = rr_node[inode].get_ptc_num();

	for (int iedge = 0, l = rr_node[inode].get_num_edges(); iedge < l; iedge++) {
		to_node = rr_node[inode].edges[iedge];
		to_type = rr_node[to_node].type;
		to_ptc_num = rr_node[to_node].get_ptc_num();

		switch (from_type) {

		case OPIN:
			switch (to_type) {
			case CHANX:
			case CHANY:
				if (draw_state->draw_rr_node[inode].color == MAGENTA) {
					// If OPIN was clicked on, set color to fan-out
					setcolor(draw_state->draw_rr_node[to_node].color);
				} else if (draw_state->draw_rr_node[to_node].color == MAGENTA) {
					// If CHANX or CHANY got clicked, set color to fan-in
					setcolor(draw_state->draw_rr_node[inode].color);
				} else
					setcolor(PINK);
				draw_pin_to_chan_edge(inode, to_node);
				break;
			case IPIN:
				if (draw_state->draw_rr_node[inode].color == MAGENTA) {
					setcolor(draw_state->draw_rr_node[to_node].color);
				} else if (draw_state->draw_rr_node[to_node].color == MAGENTA) {
					setcolor(draw_state->draw_rr_node[inode].color);
				} else
					setcolor(MEDIUMPURPLE);
				draw_pin_to_pin(inode, to_node);
				break;
			default:
				vpr_throw(VPR_ERROR_OTHER, __FILE__, __LINE__, 
						"in draw_rr_edges: node %d (type: %d) connects to node %d (type: %d).\n",
						inode, from_type, to_node, to_type);
				break;
			}
			break;

		case CHANX: /* from_type */
			switch (to_type) {
			case IPIN:
				if (draw_state->draw_rr_toggle == DRAW_NODES_AND_SBOX_RR) {
					break;
				}

				if (draw_state->draw_rr_node[to_node].node_highlighted &&
					draw_state->draw_rr_node[inode].color == DEFAULT_RR_NODE_COLOR) {
					// If the IPIN is clicked on, draw connection to all the CHANX
					// wire segments fanning into the pin. If a CHANX wire is clicked
					// on, draw only the connection between that wire and the IPIN, with
					// the pin fanning out from the wire.
					break;
				}

				if (draw_state->draw_rr_node[inode].color == MAGENTA) {
					setcolor(draw_state->draw_rr_node[to_node].color);
				} else if (draw_state->draw_rr_node[to_node].color == MAGENTA) {
					setcolor(draw_state->draw_rr_node[inode].color);
				} else
					setcolor(LIGHTSKYBLUE);
				draw_pin_to_chan_edge(to_node, inode);
				break;

			case CHANX:
				if (draw_state->draw_rr_node[inode].color == MAGENTA) {
					setcolor(draw_state->draw_rr_node[to_node].color);
				} else if (draw_state->draw_rr_node[to_node].color == MAGENTA) {
					setcolor(draw_state->draw_rr_node[inode].color);
				} else
					setcolor(DARKGREEN);
				switch_type = rr_node[inode].switches[iedge];
				draw_chanx_to_chanx_edge(inode, to_node,
						to_ptc_num, switch_type);
				break;

			case CHANY:
				if (draw_state->draw_rr_node[inode].color == MAGENTA) {
					setcolor(draw_state->draw_rr_node[to_node].color);
				} else if (draw_state->draw_rr_node[to_node].color == MAGENTA) {
					setcolor(draw_state->draw_rr_node[inode].color);
				} else
					setcolor(DARKGREEN);
				switch_type = rr_node[inode].switches[iedge];
				draw_chanx_to_chany_edge(inode, from_ptc_num, to_node,
						to_ptc_num, FROM_X_TO_Y, switch_type);
				break;

			default:
				vpr_throw(VPR_ERROR_OTHER, __FILE__, __LINE__, 
						"in draw_rr_edges: node %d (type: %d) connects to node %d (type: %d).\n",
						inode, from_type, to_node, to_type);
				break;
			}
			break;

		case CHANY: /* from_type */
			switch (to_type) {
			case IPIN:
				if (draw_state->draw_rr_toggle == DRAW_NODES_AND_SBOX_RR) {
					break;
				}

				if (draw_state->draw_rr_node[to_node].node_highlighted &&
					draw_state->draw_rr_node[inode].color == DEFAULT_RR_NODE_COLOR) {
					// If the IPIN is clicked on, draw connection to all the CHANY
					// wire segments fanning into the pin. If a CHANY wire is clicked
					// on, draw only the connection between that wire and the IPIN, with
					// the pin fanning out from the wire.
					break;
				}

				if (draw_state->draw_rr_node[inode].color == MAGENTA) {
					setcolor(draw_state->draw_rr_node[to_node].color);
				} else if (draw_state->draw_rr_node[to_node].color == MAGENTA) {
					setcolor(draw_state->draw_rr_node[inode].color);
				} else
					setcolor(LIGHTSKYBLUE);
				draw_pin_to_chan_edge(to_node, inode);
				break;

			case CHANX:
				if (draw_state->draw_rr_node[inode].color == MAGENTA) {
					setcolor(draw_state->draw_rr_node[to_node].color);
				} else if (draw_state->draw_rr_node[to_node].color == MAGENTA) {
					setcolor(draw_state->draw_rr_node[inode].color);
				} else
					setcolor(DARKGREEN);
				switch_type = rr_node[inode].switches[iedge];
				draw_chanx_to_chany_edge(to_node, to_ptc_num, inode,
						from_ptc_num, FROM_Y_TO_X, switch_type);
				break;

			case CHANY:
				if (draw_state->draw_rr_node[inode].color == MAGENTA) {
					setcolor(draw_state->draw_rr_node[to_node].color);
				} else if (draw_state->draw_rr_node[to_node].color == MAGENTA) {
					setcolor(draw_state->draw_rr_node[inode].color);
				} else
					setcolor(DARKGREEN);
				switch_type = rr_node[inode].switches[iedge];
				draw_chany_to_chany_edge(inode, to_node,
						to_ptc_num, switch_type);
				break;

			default:
				vpr_throw(VPR_ERROR_OTHER, __FILE__, __LINE__, 
						"in draw_rr_edges: node %d (type: %d) connects to node %d (type: %d).\n",
						inode, from_type, to_node, to_type);
				break;
			}
			break;

		default: /* from_type */
			vpr_throw(VPR_ERROR_OTHER, __FILE__, __LINE__, 
					"draw_rr_edges called with node %d of type %d.\n", 
					inode, from_type);
			break;
		}
	} /* End of for each edge loop */
}

static void draw_x(float x, float y, float size) {

	/* Draws an X centered at (x,y).  The width and height of the X are each    *
	 * 2 * size.                                                                */

	drawline(x - size, y + size, x + size, y - size);
	drawline(x - size, y - size, x + size, y + size);
}


static void draw_chanx_to_chany_edge(int chanx_node, int chanx_track,
		int chany_node, int chany_track, enum e_edge_dir edge_dir,
		short switch_type) {

	t_draw_state* draw_state = get_draw_state_vars();
	t_draw_coords* draw_coords = get_draw_coords_vars();

	/* Draws an edge (SBOX connection) between an x-directed channel and a    *
	 * y-directed channel.                                                    */

	float x1, y1, x2, y2;
	t_bound_box chanx_bbox, chany_bbox;
	int chanx_xlow, chany_x, chany_ylow, chanx_y;

	/* Get the coordinates of the CHANX and CHANY segments. */
	chanx_bbox = draw_get_rr_chan_bbox(chanx_node);
	chany_bbox = draw_get_rr_chan_bbox(chany_node);

	/* (x1,y1): point on CHANX segment, (x2,y2): point on CHANY segment. */

	y1 = chanx_bbox.bottom(); 
	x2 = chany_bbox.left();

	chanx_xlow = rr_node[chanx_node].get_xlow();
	chanx_y = rr_node[chanx_node].get_ylow();
	chany_x = rr_node[chany_node].get_xlow();
	chany_ylow = rr_node[chany_node].get_ylow();

	if (chanx_xlow <= chany_x) { /* Can draw connection going right */
		/* Connection not at end of the CHANX segment. */
		x1 = draw_coords->tile_x[chany_x] + draw_coords->get_tile_width();
		
		if (rr_node[chanx_node].get_direction() != BI_DIRECTION) {
			if (edge_dir == FROM_X_TO_Y) {
				if ((chanx_track % 2) == 1) { /* If dec wire, then going left */
					x1 = draw_coords->tile_x[chany_x + 1];
				}
			}
		}
		
	} else { /* Must draw connection going left. */
		x1 = chanx_bbox.left();
	}

	if (chany_ylow <= chanx_y) { /* Can draw connection going up. */
		/* Connection not at end of the CHANY segment. */
		y2 = draw_coords->tile_y[chanx_y] + draw_coords->get_tile_width();
		
		if (rr_node[chany_node].get_direction() != BI_DIRECTION) {
			if (edge_dir == FROM_Y_TO_X) {
				if ((chany_track % 2) == 1) { /* If dec wire, then going down */
					y2 = draw_coords->tile_y[chanx_y + 1];
				}
			}
		}
		
	} else { /* Must draw connection going down. */
		y2 = chany_bbox.bottom();
	}

	drawline(x1, y1, x2, y2);

	if (draw_state->draw_rr_toggle == DRAW_ALL_RR || draw_state->draw_rr_node[chanx_node].node_highlighted) {
        if (edge_dir == FROM_X_TO_Y) {
            draw_rr_switch(x1, y1, x2, y2, g_rr_switch_inf[switch_type].buffered);
        } else {
            draw_rr_switch(x2, y2, x1, y1, g_rr_switch_inf[switch_type].buffered);
        }
    }
}


static void draw_chanx_to_chanx_edge(int from_node, int to_node,
		int to_track, short switch_type) {

	/* Draws a connection between two x-channel segments.  Passing in the track *
	 * numbers allows this routine to be used for both rr_graph and routing     *
	 * drawing.                                                                 */

	t_draw_state* draw_state = get_draw_state_vars();
	t_draw_coords* draw_coords = get_draw_coords_vars();

	float x1, x2, y1, y2;
	t_bound_box from_chan, to_chan;
	int from_xlow, to_xlow, from_xhigh, to_xhigh;
	
	// Get the coordinates of the channel wires.
	from_chan = draw_get_rr_chan_bbox(from_node);
	to_chan = draw_get_rr_chan_bbox(to_node);

	/* (x1, y1) point on from_node, (x2, y2) point on to_node. */

	y1 = from_chan.bottom();
	y2 = to_chan.bottom();

	from_xlow = rr_node[from_node].get_xlow();
	from_xhigh = rr_node[from_node].get_xhigh();
	to_xlow = rr_node[to_node].get_xlow();
	to_xhigh = rr_node[to_node].get_xhigh();

	if (to_xhigh < from_xlow) { /* From right to left */
		/* UDSD Note by WMF: could never happen for INC wires, unless U-turn. For DEC 
		 * wires this handles well */
		x1 = from_chan.left();
		x2 = to_chan.right();
	} else if (to_xlow > from_xhigh) { /* From left to right */
		/* UDSD Note by WMF: could never happen for DEC wires, unless U-turn. For INC 
		 * wires this handles well */
		x1 = from_chan.right();
		x2 = to_chan.left();
	}

	/* Segments overlap in the channel.  Figure out best way to draw.  Have to  *
	 * make sure the drawing is symmetric in the from rr and to rr so the edges *
	 * will be drawn on top of each other for bidirectional connections.        */

	else {
		if (rr_node[to_node].get_direction() != BI_DIRECTION) {
			/* must connect to to_node's wire beginning at x2 */
			if (to_track % 2 == 0) { /* INC wire starts at leftmost edge */
				VTR_ASSERT(from_xlow < to_xlow);
				x2 = to_chan.left();
				/* since no U-turns from_track must be INC as well */
				x1 = draw_coords->tile_x[to_xlow - 1] + draw_coords->get_tile_width();
			} else { /* DEC wire starts at rightmost edge */
				VTR_ASSERT(from_xhigh > to_xhigh);
				x2 = to_chan.right();
				x1 = draw_coords->tile_x[to_xhigh + 1];
			}
		} else {
			if (to_xlow < from_xlow) { /* Draw from left edge of one to other */
				x1 = from_chan.left();
				x2 = draw_coords->tile_x[from_xlow - 1] + draw_coords->get_tile_width();
			} else if (from_xlow < to_xlow) {
				x1 = draw_coords->tile_x[to_xlow - 1] + draw_coords->get_tile_width();
				x2 = to_chan.left();
			} /* The following then is executed when from_xlow == to_xlow */
			else if (to_xhigh > from_xhigh) { /* Draw from right edge of one to other */
				x1 = from_chan.right();
				x2 = draw_coords->tile_x[from_xhigh + 1];
			} else if (from_xhigh > to_xhigh) {
				x1 = draw_coords->tile_x[to_xhigh + 1];
				x2 = to_chan.right();
			} else { /* Complete overlap: start and end both align. Draw outside the sbox */
				x1 = from_chan.left();
				x2 = from_chan.left() + draw_coords->get_tile_width();
			}
		}
	}
	
	drawline(x1, y1, x2, y2);

	if (draw_state->draw_rr_toggle == DRAW_ALL_RR || draw_state->draw_rr_node[from_node].node_highlighted) {
		draw_rr_switch(x1, y1, x2, y2, g_rr_switch_inf[switch_type].buffered);
	}
}


static void draw_chany_to_chany_edge(int from_node, int to_node,
		int to_track, short switch_type) {

	t_draw_state* draw_state = get_draw_state_vars();
	t_draw_coords* draw_coords = get_draw_coords_vars();
	
	/* Draws a connection between two y-channel segments.  Passing in the track *
	 * numbers allows this routine to be used for both rr_graph and routing     *
	 * drawing.                                                                 */

	float x1, x2, y1, y2;
	t_bound_box from_chan, to_chan;
	int from_ylow, to_ylow, from_yhigh, to_yhigh;//, from_x, to_x;

	// Get the coordinates of the channel wires.
	from_chan = draw_get_rr_chan_bbox(from_node);
	to_chan = draw_get_rr_chan_bbox(to_node);

	// from_x = rr_node[from_node].get_xlow();
	// to_x = rr_node[to_node].get_xlow();
	from_ylow = rr_node[from_node].get_ylow();
	from_yhigh = rr_node[from_node].get_yhigh();
	to_ylow = rr_node[to_node].get_ylow();
	to_yhigh = rr_node[to_node].get_yhigh();

	/* (x1, y1) point on from_node, (x2, y2) point on to_node. */

	x1 = from_chan.left();
	x2 = to_chan.left();

	if (to_yhigh < from_ylow) { /* From upper to lower */
		y1 = from_chan.bottom();
		y2 = to_chan.top();
	} else if (to_ylow > from_yhigh) { /* From lower to upper */
		y1 = from_chan.top();
		y2 = to_chan.bottom();
	}

	/* Segments overlap in the channel.  Figure out best way to draw.  Have to  *
	 * make sure the drawing is symmetric in the from rr and to rr so the edges *
	 * will be drawn on top of each other for bidirectional connections.        */

	/* UDSD Modification by WMF Begin */
	else {
		if (rr_node[to_node].get_direction() != BI_DIRECTION) {
			if (to_track % 2 == 0) { /* INC wire starts at bottom edge */

				y2 = to_chan.bottom();
				/* since no U-turns from_track must be INC as well */
				y1 = draw_coords->tile_y[to_ylow - 1] + draw_coords->get_tile_width();
			} else { /* DEC wire starts at top edge */
				
				y2 = to_chan.top();
				y1 = draw_coords->tile_y[to_yhigh + 1];
			}
		} else {
			if (to_ylow < from_ylow) { /* Draw from bottom edge of one to other. */
				y1 = from_chan.bottom();
				y2 = draw_coords->tile_y[from_ylow - 1] + draw_coords->get_tile_width();
			} else if (from_ylow < to_ylow) {
				y1 = draw_coords->tile_y[to_ylow - 1] + draw_coords->get_tile_width();
				y2 = to_chan.bottom();
			} else if (to_yhigh > from_yhigh) { /* Draw from top edge of one to other. */
				y1 = from_chan.top();
				y2 = draw_coords->tile_y[from_yhigh + 1];
			} else if (from_yhigh > to_yhigh) {
				y1 = draw_coords->tile_y[to_yhigh + 1];
				y2 = to_chan.top();
			} else { /* Complete overlap: start and end both align. Draw outside the sbox */
				y1 = from_chan.bottom();
				y2 = from_chan.bottom() + draw_coords->get_tile_width();
			}
		}
	}


	/* UDSD Modification by WMF End */
	drawline(x1, y1, x2, y2);

	if (draw_state->draw_rr_toggle == DRAW_ALL_RR || draw_state->draw_rr_node[from_node].node_highlighted) {
		draw_rr_switch(x1, y1, x2, y2, g_rr_switch_inf[switch_type].buffered);
	}
}


/* This function computes and returns the boundary coordinates of a channel
 * wire segment. This can be used for drawing a wire or determining if a 
 * wire has been clicked on by the user. 
 * TODO: Fix this for global routing, currently for detailed only. 
 */
static t_bound_box draw_get_rr_chan_bbox (int inode) {
	t_bound_box bound_box;

	t_draw_coords* draw_coords = get_draw_coords_vars();

	switch (rr_node[inode].type) {
		case CHANX:
			bound_box.left() = draw_coords->tile_x[rr_node[inode].get_xlow()];
	        bound_box.right() = draw_coords->tile_x[rr_node[inode].get_xhigh()] 
						        + draw_coords->get_tile_width();
			bound_box.bottom() = draw_coords->tile_y[rr_node[inode].get_ylow()] 
								+ draw_coords->get_tile_width() 
								+ (1. + rr_node[inode].get_ptc_num());
			bound_box.top() = draw_coords->tile_y[rr_node[inode].get_ylow()] 
								+ draw_coords->get_tile_width() 
								+ (1. + rr_node[inode].get_ptc_num());
			break;
		case CHANY:
			bound_box.left() = draw_coords->tile_x[rr_node[inode].get_xlow()] 
								+ draw_coords->get_tile_width() 
								+ (1. + rr_node[inode].get_ptc_num());
			bound_box.right() = draw_coords->tile_x[rr_node[inode].get_xlow()] 
								+ draw_coords->get_tile_width() 
								+ (1. + rr_node[inode].get_ptc_num());
			bound_box.bottom() = draw_coords->tile_y[rr_node[inode].get_ylow()];
			bound_box.top() = draw_coords->tile_y[rr_node[inode].get_yhigh()]
			                    + draw_coords->get_tile_width();
			break;
		default:
			// a problem. leave at default value (ie. zeros)
			break;
	}


	return bound_box;
}


static void draw_rr_switch(float from_x, float from_y, float to_x, float to_y, bool buffered) {

	/* Draws a buffer (triangle) or pass transistor (circle) on the edge        *
	 * connecting from to to, depending on the status of buffered.  The drawing *
	 * is closest to the from_node, since it reflects the switch type of from.  */

	if (!buffered) { /* Draw a circle for a pass transistor */
        float xcen = from_x + (to_x - from_x) / 10.;
        float ycen = from_y + (to_y - from_y) / 10.;
        const float switch_rad = 0.15;
		drawarc(xcen, ycen, switch_rad, 0., 360.);
	} else { /* Buffer */
        if(from_x == to_x || from_y == to_y) {
            //Straight connection
            draw_triangle_along_line(t_point(from_x, from_y), t_point(to_x, to_y), SB_EDGE_STRAIGHT_ARROW_POSITION);
        } else {
            //Turn connection
            draw_triangle_along_line(t_point(from_x, from_y), t_point(to_x, to_y), SB_EDGE_TURN_ARROW_POSITION);
        }
	}
}

static void draw_rr_pin(int inode, const t_color& color) {

	/* Draws an IPIN or OPIN rr_node.  Note that the pin can appear on more    *
	 * than one side of a clb.  Also note that this routine can change the     *
	 * current color to BLACK.                                                 */


	t_draw_coords* draw_coords = get_draw_coords_vars();

	//exit early unless zoomed in really far.
    if (LOD_screen_area_test_square(draw_coords->pin_size, MIN_VISIBLE_AREA) == false) {
        return;
    }

	int ipin, i, j, iside;
	float xcen, ycen;
	char str[vtr::BUFSIZE];
	t_type_ptr type;

	i = rr_node[inode].get_xlow();
	j = rr_node[inode].get_ylow();
	ipin = rr_node[inode].get_ptc_num();
	type = grid[i][j].type;
	int width_offset = grid[i][j].width_offset;
	int height_offset = grid[i][j].height_offset;

	setcolor(color);

	/* TODO: This is where we can hide fringe physical pins and also identify globals (hide, color, show) */
	for (iside = 0; iside < 4; iside++) {
		if (type->pinloc[grid[i][j].width_offset][grid[i][j].height_offset][iside][ipin]) { /* Pin exists on this side. */
			draw_get_rr_pin_coords(inode, iside, width_offset, height_offset, &xcen, &ycen);
			fillrect(xcen - draw_coords->pin_size, ycen - draw_coords->pin_size, 
					 xcen + draw_coords->pin_size, ycen + draw_coords->pin_size);
			sprintf(str, "%d", ipin);
			setcolor(BLACK);
			drawtext(xcen, ycen, str, 2 * draw_coords->pin_size, 2 * draw_coords->pin_size);
			setcolor(color);
		}
	}
}

/* Returns the coordinates at which the center of this pin should be drawn. *
 * inode gives the node number, and iside gives the side of the clb or pad  *
 * the physical pin is on.                                                  */
void draw_get_rr_pin_coords(int inode, int iside, 
		int width_offset, int height_offset, 
		float *xcen, float *ycen) {
	draw_get_rr_pin_coords(&rr_node[inode], iside, width_offset, height_offset, xcen, ycen);
}

void draw_get_rr_pin_coords(t_rr_node* node, int iside, 
		int width_offset, int height_offset, 
		float *xcen, float *ycen) {

	t_draw_coords* draw_coords = get_draw_coords_vars();

	int i, j, k, ipin, pins_per_sub_tile;
	float offset, xc, yc, step;
	t_type_ptr type;

	i = node->get_xlow() + width_offset;
	j = node->get_ylow() + height_offset;

	xc = draw_coords->tile_x[i];
	yc = draw_coords->tile_y[j];

	ipin = node->get_ptc_num();
	type = grid[i][j].type;
	pins_per_sub_tile = grid[i][j].type->num_pins / grid[i][j].type->capacity;
	k = ipin / pins_per_sub_tile;

	/* Since pins numbers go across all sub_tiles in a block in order
	 * we can treat as a block box for this step */

	/* For each sub_tile we need and extra padding space */
	step = (float) (draw_coords->get_tile_width()) / (float) (type->num_pins + type->capacity);
	offset = (ipin + k + 1) * step;

	switch (iside) {
	case LEFT:
		yc += offset;
		break;

	case RIGHT:
		xc += draw_coords->get_tile_width();
		yc += offset;
		break;

	case BOTTOM:
		xc += offset;
		break;

	case TOP:
		xc += offset;
		yc += draw_coords->get_tile_width();
		break;

	default:
		vpr_throw(VPR_ERROR_OTHER, __FILE__, __LINE__, 
				"in draw_get_rr_pin_coords: Unexpected iside %d.\n", iside);
		break;
	}

	*xcen = xc;
	*ycen = yc;
}


static void drawroute(enum e_draw_net_type draw_net_type) {

	/* Draws the nets in the positions fixed by the router.  If draw_net_type is *
	 * ALL_NETS, draw all the nets.  If it is HIGHLIGHTED, draw only the nets    *
	 * that are not coloured black (useful for drawing over the rr_graph).       */

	t_draw_state* draw_state = get_draw_state_vars();

	/* Next free track in each channel segment if routing is GLOBAL */

	unsigned int inet;
	int inode;
	struct s_trace *tptr;
	t_rr_type rr_type;

	setlinestyle(SOLID);

	/* Now draw each net, one by one.      */

	for (inet = 0; inet < g_clbs_nlist.net.size(); inet++) {
		if (g_clbs_nlist.net[inet].is_global) /* Don't draw global nets. */
			continue;

		if (trace_head[inet] == NULL) /* No routing.  Skip.  (Allows me to draw */
			continue; /* partially complete routes).            */

		if (draw_net_type == HIGHLIGHTED && draw_state->net_color[inet] == BLACK)
			continue;

		tptr = trace_head[inet]; /* SOURCE to start */
		inode = tptr->index;
		rr_type = rr_node[inode].type;

        std::vector<int> rr_nodes_to_draw;
        rr_nodes_to_draw.push_back(inode);
		for (;;) {
			tptr = tptr->next;
			inode = tptr->index;
			rr_type = rr_node[inode].type;

			if (draw_if_net_highlighted(inet)) {
				/* If a net has been highlighted, highlight the whole net in *
				 * the same color.											 */
				draw_state->draw_rr_node[inode].color = draw_state->net_color[inet];
				draw_state->draw_rr_node[inode].node_highlighted = true;
			}
			else {
				/* If not highlighted, draw the node in default color. */
				draw_state->draw_rr_node[inode].color = DEFAULT_RR_NODE_COLOR;
			}

            rr_nodes_to_draw.push_back(inode);

			if (rr_type == SINK) { /* Skip the next segment */
                draw_partial_route(rr_nodes_to_draw);
                rr_nodes_to_draw.clear();

				tptr = tptr->next;
				if (tptr == NULL)
					break;
				inode = tptr->index;
				rr_type = rr_node[inode].type;
                rr_nodes_to_draw.push_back(inode);

			}

		} /* End loop over traceback. */

        draw_partial_route(rr_nodes_to_draw);
	} /* End for (each net) */
}

//Draws the set of rr_nodes specified, using the colors set in draw_state
void draw_partial_route(const std::vector<int>& rr_nodes_to_draw) {

	t_draw_state* draw_state = get_draw_state_vars();

	static int **chanx_track = NULL; /* [1..nx][0..ny] */
	static int **chany_track = NULL; /* [0..nx][1..ny] */
	if (draw_state->draw_route_type == GLOBAL) {
		/* Allocate some temporary storage if it's not already available. */
		if (chanx_track == NULL) {
			chanx_track = vtr::alloc_matrix<int>(1, nx, 0, ny);
		}

		if (chany_track == NULL) {
			chany_track = vtr::alloc_matrix<int>(0, nx, 1, ny);
		}

		for (int i = 1; i <= nx; i++)
			for (int j = 0; j <= ny; j++)
				chanx_track[i][j] = (-1);

		for (int i = 0; i <= nx; i++)
			for (int j = 1; j <= ny; j++)
				chany_track[i][j] = (-1);
	}

    for(size_t i = 1; i < rr_nodes_to_draw.size(); ++i) {

        int inode = rr_nodes_to_draw[i];
        auto rr_type = rr_node[inode].type;

        int prev_node = rr_nodes_to_draw[i-1];
        auto prev_type = rr_node[prev_node].type;

        auto switch_type = find_switch(prev_node, inode);

        switch (rr_type) {

            case OPIN: {
                draw_rr_pin(inode, draw_state->draw_rr_node[inode].color);
                break;
            }
            case IPIN: {
                draw_rr_pin(inode, draw_state->draw_rr_node[inode].color);
                if(rr_node[prev_node].type == OPIN) {
                    draw_pin_to_pin(prev_node, inode);
                } else {
                    draw_pin_to_chan_edge(inode, prev_node);
                }
                break;
            }
            case CHANX: {
                if (draw_state->draw_route_type == GLOBAL)
                    chanx_track[rr_node[inode].get_xlow()][rr_node[inode].get_ylow()]++;

                int itrack = get_track_num(inode, chanx_track, chany_track);
                draw_rr_chanx(inode, draw_state->draw_rr_node[inode].color);

                switch (prev_type) {

                    case CHANX: {
                        draw_chanx_to_chanx_edge(prev_node, inode,
                                itrack, switch_type);
                        break;
                    }
                    case CHANY: {
                        int prev_track = get_track_num(prev_node, chanx_track,
                                chany_track);
                        draw_chanx_to_chany_edge(inode, itrack, prev_node,
                                prev_track, FROM_Y_TO_X, switch_type);
                        break;
                    }
                    case OPIN: {
                        draw_pin_to_chan_edge(prev_node, inode);
                        break;
                    }
                    default: {
                        vpr_throw(VPR_ERROR_OTHER, __FILE__, __LINE__, 
                                "Unexpected connection from an rr_node of type %d to one of type %d.\n",
                                prev_type, rr_type);
                    }
                }

                break;
            }
            case CHANY: {
                if (draw_state->draw_route_type == GLOBAL)
                    chany_track[rr_node[inode].get_xlow()][rr_node[inode].get_ylow()]++;

                int itrack = get_track_num(inode, chanx_track, chany_track);
                draw_rr_chany(inode, draw_state->draw_rr_node[inode].color);

                switch (prev_type) {

                    case CHANX: {
                        int prev_track = get_track_num(prev_node, chanx_track,
                                chany_track);
                        draw_chanx_to_chany_edge(prev_node, prev_track, inode,
                                itrack, FROM_X_TO_Y, switch_type);
                        break;
                    }
                    case CHANY: {
                        draw_chany_to_chany_edge(prev_node, inode,
                                itrack, switch_type);
                        break;
                    }
                    case OPIN: {
                        draw_pin_to_chan_edge(prev_node, inode);

                        break;
                    }
                    default: {
                        vpr_throw(VPR_ERROR_OTHER, __FILE__, __LINE__, 
                                "Unexpected connection from an rr_node of type %d to one of type %d.\n",
                                prev_type, rr_type);
                    }
                }

                break;
            }
            default: {
                break;
            }

        }
    }
}

static int get_track_num(int inode, int **chanx_track, int **chany_track) {

	/* Returns the track number of this routing resource node.   */

	int i, j;
	t_rr_type rr_type;

	if (get_draw_state_vars()->draw_route_type == DETAILED)
		return (rr_node[inode].get_ptc_num());

	/* GLOBAL route stuff below. */

	rr_type = rr_node[inode].type;
	i = rr_node[inode].get_xlow(); /* NB: Global rr graphs must have only unit */
	j = rr_node[inode].get_ylow(); /* length channel segments.                 */

	switch (rr_type) {
	case CHANX:
		return (chanx_track[i][j]);

	case CHANY:
		return (chany_track[i][j]);

	default:
		vpr_throw(VPR_ERROR_OTHER, __FILE__, __LINE__, 
				"in get_track_num: Unexpected node type %d for node %d.\n", rr_type, inode);
		return OPEN;
	}
}


/* This helper function determines whether a net has been highlighted. The highlighting *
 * could be caused by the user clicking on a routing resource, highlight critical path  *
 * toggled, or fan-in/fan-out of a highlighted node.									*/
static bool draw_if_net_highlighted (int inet) {
	bool highlighted = false;

	t_draw_state* draw_state = get_draw_state_vars();

	if (draw_state->net_color[inet] == MAGENTA
		|| draw_state->net_color[inet] == DRIVES_IT_COLOR
		|| draw_state->net_color[inet] == DRIVEN_BY_IT_COLOR
		|| draw_state->net_color[inet] == crit_path_colors::net::HEAD
		|| draw_state->net_color[inet] == crit_path_colors::net::TAIL) {

		highlighted = true;
	}

	return highlighted;
}


/* If an rr_node has been clicked on, it will be highlighted in MAGENTA.
 * If so, and toggle nets is selected, highlight the whole net in that colour.
 */
static void highlight_nets(char *message, int hit_node) {
	unsigned int inet;
	struct s_trace *tptr;

	t_draw_state* draw_state = get_draw_state_vars();
	
	for (inet = 0; inet < g_clbs_nlist.net.size(); inet++) {
		for (tptr = trace_head[inet]; tptr != NULL; tptr = tptr->next) {
			if (draw_state->draw_rr_node[tptr->index].color == MAGENTA) {
				draw_state->net_color[inet] = draw_state->draw_rr_node[tptr->index].color;
				if (tptr->index == hit_node) {
					sprintf(message, "%s  ||  Net: %d (%s)", message, inet,
							g_clbs_nlist.net[inet].name);
				}
			}
			else if (draw_state->draw_rr_node[tptr->index].color == WHITE) {
				// If node is de-selected.
				draw_state->net_color[inet] = BLACK;
				break;
			}
		}
	}
	update_message(message);
}


/* If an rr_node has been clicked on, it will be either highlighted in MAGENTA,
 * or de-highlighted in WHITE. If highlighted, and toggle_rr is selected, highlight 
 * fan_in into the node in blue and fan_out from the node in red. If de-highlighted,
 * de-highlight its fan_in and fan_out.
 */
static void draw_highlight_fan_in_fan_out(int hit_node) {
	int inode;

	t_draw_state* draw_state = get_draw_state_vars();

	/* Highlight the fanout nodes in red. */
	for (int iedge = 0, l = rr_node[hit_node].get_num_edges(); iedge < l; iedge++) {
		int fanout_node = rr_node[hit_node].edges[iedge];

		if (draw_state->draw_rr_node[hit_node].color == MAGENTA) {
			// If node is highlighted, highlight its fanout
			draw_state->draw_rr_node[fanout_node].color = DRIVES_IT_COLOR;
			draw_state->draw_rr_node[fanout_node].node_highlighted = true;
		}
		else if (draw_state->draw_rr_node[hit_node].color == WHITE) {
			// If node is de-highlighted, de-highlight its fanout
			draw_state->draw_rr_node[fanout_node].color = DEFAULT_RR_NODE_COLOR;
			draw_state->draw_rr_node[fanout_node].node_highlighted = false;
		}
	}

	/* Highlight the nodes that can fanin to this node in blue. */
	for (inode = 0; inode < num_rr_nodes; inode++) {
		for (int iedge = 0, l = rr_node[inode].get_num_edges(); iedge < l; iedge++) {
			int fanout_node = rr_node[inode].edges[iedge];
			if (fanout_node == hit_node) { 
				if (draw_state->draw_rr_node[hit_node].color == MAGENTA) {
					// If hit_node is highlighted, highlight its fanin
					draw_state->draw_rr_node[inode].color = BLUE;  
					draw_state->draw_rr_node[inode].node_highlighted = true;
				}
				else if (draw_state->draw_rr_node[hit_node].color == WHITE) {
					// If hit_node is de-highlighted, de-highlight its fanin
					draw_state->draw_rr_node[inode].color = DEFAULT_RR_NODE_COLOR;
					draw_state->draw_rr_node[inode].node_highlighted = false;
				}
			}
		}
	}
}


/* This is a helper function for highlight_rr_nodes(). It determines whether 
 * a routing resource has been clicked on by computing a bounding box for that 
 *  and checking if the mouse click hit inside its bounding box.
 *
 *  It returns the hit RR node's ID (or OPEN if no hit)
 */
static int draw_check_rr_node_hit (float click_x, float click_y) {
	int inode;
	int hit_node = OPEN;
	t_bound_box bound_box;

	t_draw_coords* draw_coords = get_draw_coords_vars();

	for (inode = 0; inode < num_rr_nodes; inode++) {
		switch (rr_node[inode].type) {
			case IPIN:
			case OPIN:		
			{
				int i = rr_node[inode].get_xlow();
				int j = rr_node[inode].get_ylow();
				t_type_ptr type = grid[i][j].type;
				int width_offset = grid[i][j].width_offset;
				int height_offset = grid[i][j].height_offset;
				int ipin = rr_node[inode].get_ptc_num();
				float xcen, ycen;
				
				int iside;
				for (iside = 0; iside < 4; iside++) {
					// If pin exists on this side of the block, then get pin coordinates
					if (type->pinloc[width_offset][height_offset][iside][ipin]) {
						draw_get_rr_pin_coords(inode, iside, width_offset, height_offset, 
											   &xcen, &ycen);

						// Now check if we clicked on this pin
						if (click_x >= xcen - draw_coords->pin_size &&
							click_x <= xcen + draw_coords->pin_size &&
							click_y >= ycen - draw_coords->pin_size && 
							click_y <= ycen + draw_coords->pin_size) {
							hit_node = inode;
							return hit_node;
						}
					}
				}
				break;
			}
			case CHANX:
			case CHANY:
			{
				bound_box = draw_get_rr_chan_bbox(inode);

				// Check if we clicked on this wire, with 30%
				// tolerance outside its boundary
				const float tolerance = 0.3;
				if (click_x >= bound_box.left() - tolerance &&
					click_x <= bound_box.right() + tolerance &&
					click_y >= bound_box.bottom() - tolerance && 
					click_y <= bound_box.top() + tolerance) {
					hit_node = inode;
					return hit_node;
				}
				break;
			}
			default:
				break;
		}
	}
	return hit_node;
}


/* This routine is called when the routing resource graph is shown, and someone 
 * clicks outside a block. That click might represent a click on a wire -- we call
 * this routine to determine which wire (if any) was clicked on.  If a wire was
 * clicked upon, we highlight it in Magenta, and its fanout in red. 
 */
static void highlight_rr_nodes(float x, float y) {

	t_draw_state* draw_state = get_draw_state_vars();

	int hit_node = OPEN;  // i.e. -1, no node selected.
	char message[250] = "";

	if (draw_state->draw_rr_toggle == DRAW_NO_RR && !draw_state->show_nets) {
		update_message(draw_state->default_message);
		drawscreen();
		return;
	}

	// Check which rr_node (if any) was clicked on.
	hit_node = draw_check_rr_node_hit (x, y);

	if (hit_node != OPEN) {
		int xlow = rr_node[hit_node].get_xlow();
		int xhigh = rr_node[hit_node].get_xhigh();
		int ylow = rr_node[hit_node].get_ylow();
		int yhigh = rr_node[hit_node].get_yhigh();
		int ptc_num = rr_node[hit_node].get_ptc_num();

		if (draw_state->draw_rr_node[hit_node].color != MAGENTA) {
			/* If the node hasn't been clicked on before, highlight it
			 * in magenta.
			 */
			draw_state->draw_rr_node[hit_node].color = MAGENTA;
			draw_state->draw_rr_node[hit_node].node_highlighted = true;

			sprintf(message, "Selected node #%d: %s (%d,%d) -> (%d,%d) track: %d, %d edges, occ: %d, capacity: %d",
				    hit_node, rr_node[hit_node].rr_get_type_string(),
				    xlow, ylow, xhigh, yhigh, ptc_num, rr_node[hit_node].get_num_edges(), 
				    rr_node[hit_node].get_occ(), rr_node[hit_node].get_capacity());

            rr_highlight_message = message;

		}
		else {
			/* Using white color to represent de-highlighting (or 
			 * de-selecting) of node.
			 */
			draw_state->draw_rr_node[hit_node].color = WHITE;
			draw_state->draw_rr_node[hit_node].node_highlighted = false;
		}

		print_rr_node(stdout, rr_node, hit_node);
		if (draw_state->draw_rr_toggle != DRAW_NO_RR) 
			// If rr_graph is shown, highlight the fan-in/fan-outs for
			// this node.
			draw_highlight_fan_in_fan_out(hit_node);
   }

	if (hit_node == OPEN) {
		update_message(draw_state->default_message);
        rr_highlight_message = "";
		drawscreen();
		return;
	}

	if (draw_state->show_nets) {
		highlight_nets(message, hit_node);
	} else
		update_message(message);

	drawscreen();
}


static void highlight_blocks(float abs_x, float abs_y, t_event_buttonPressed button_info) {

	/* This routine is called when the user clicks in the graphics area. *
	 * It determines if a clb was clicked on.  If one was, it is         *
	 * highlighted in green, it's fanin nets and clbs are highlighted in *
	 * blue and it's fanout is highlighted in red.  If no clb was        *
	 * clicked on (user clicked on white space) any old highlighting is  *
	 * removed.  Note that even though global nets are not drawn, their  *
	 * fanins and fanouts are highlighted when you click on a block      *
	 * attached to them.                                                 */

	t_draw_coords* draw_coords = get_draw_coords_vars();

	char msg[vtr::BUFSIZE];
	int clb_index = -2;

	/* Control + mouse click to select multiple nets. */
	if (!button_info.ctrl_pressed)
		deselect_all();

	/// determine block ///

	t_block* clb = NULL;
	t_bound_box clb_bbox(0,0,0,0);

	// iterate over grid x
	for (int i = 0; i <= nx + 1; ++i) {
		if (draw_coords->tile_x[i] > abs_x) {
			break; // we've gone to far in the x direction
		}
		// iterate over grid y
		for(int j = 0; j <= ny + 1; ++j) {
			if (draw_coords->tile_y[j] > abs_y) {
				break; // we've gone to far in the y direction
			}
			// iterate over sub_blocks
			t_grid_tile* grid_tile = &grid[i][j];
			for (int k = 0; k < grid_tile->type->capacity; ++k) {
				clb_index = grid_tile->blocks[k];
				if (clb_index != EMPTY_BLOCK) {
					clb = &block[clb_index];
					clb_bbox = draw_coords->get_absolute_clb_bbox(*clb);
					if (clb_bbox.intersects(abs_x, abs_y)) {
						break;
					} else {
						clb = NULL;
					}
				}
			}
			if (clb != NULL) {
				break; // we've found something
			}
		}
		if (clb != NULL) {
			break; // we've found something
		}
	}

	if (clb == NULL) {
		highlight_rr_nodes(abs_x, abs_y);
		/* update_message(draw_state->default_message);
		 drawscreen(); */
		return;
	} 

	VTR_ASSERT(clb_index != EMPTY_BLOCK);

	// note: this will clear the selected sub-block if show_blk_internal is 0,
	// or if it doesn't find anything
	t_point point_in_clb = t_point(abs_x, abs_y) - clb_bbox.bottom_left();
	highlight_sub_block(point_in_clb, *clb);
	
	if (get_selected_sub_block_info().has_selection()) {
		t_pb* selected_subblock = get_selected_sub_block_info().get_selected_pb();
		sprintf(msg, "sub-block %s (a \"%s\") selected", 
			selected_subblock->name, selected_subblock->pb_graph_node->pb_type->name);
	} else {
		/* Highlight block and fan-in/fan-outs. */
		draw_highlight_blocks_color(clb->type, clb_index);
		sprintf(msg, "Block #%d (%s) at (%d, %d) selected.", clb_index, clb->name, clb->x, clb->y);
	}

	update_message(msg);

	drawscreen(); /* Need to erase screen. */
}

static void act_on_mouse_over(float mouse_x, float mouse_y) {

	t_draw_state* draw_state = get_draw_state_vars();

	if (draw_state->draw_rr_toggle != DRAW_NO_RR) {

        int hit_node = draw_check_rr_node_hit(mouse_x, mouse_y);

        if(hit_node != OPEN) {
            //Update message

            std::string msg = vtr::string_fmt("Moused over rr node #%d: %s", hit_node, rr_node[hit_node].rr_get_type_string());
            if (rr_node[hit_node].type == CHANX || rr_node[hit_node].type == CHANY) {
                msg += vtr::string_fmt(" track: %d len: %d", rr_node[hit_node].get_ptc_num(), rr_node[hit_node].get_length());
                update_message(msg.c_str());
            } else if (rr_node[hit_node].type == IPIN || rr_node[hit_node].type == OPIN) {
                msg += vtr::string_fmt(" pin: %d len: %d", rr_node[hit_node].get_ptc_num(), rr_node[hit_node].get_length());
                update_message(msg.c_str());
            }
        } else {
            //No rr node moused over, reset message
            if(!rr_highlight_message.empty()) {
                update_message(rr_highlight_message.c_str());
            } else {
                update_message(draw_state->default_message);
            }
        }
    }
}


static void draw_highlight_blocks_color(t_type_ptr type, int bnum) {
	int k, netnum, fanblk, iclass;
	unsigned ipin;

	t_draw_state* draw_state = get_draw_state_vars();

	for (k = 0; k < type->num_pins; k++) { /* Each pin on a CLB */
		netnum = block[bnum].nets[k];

		if (netnum == OPEN)
			continue;

		iclass = type->pin_class[k];

		if (type->class_inf[iclass].type == DRIVER) { /* Fanout */
			if (draw_state->block_color[bnum] == SELECTED_COLOR) {
				/* If block already highlighted, de-highlight the fanout. (the deselect case)*/
				draw_state->net_color[netnum] = BLACK;
				for (ipin = 1; ipin < g_clbs_nlist.net[netnum].pins.size(); ipin++) {
					fanblk = g_clbs_nlist.net[netnum].pins[ipin].block;
					draw_reset_blk_color(fanblk);
				}
			}
			else {
				/* Highlight the fanout */
				draw_state->net_color[netnum] = DRIVES_IT_COLOR;
				for (ipin = 1; ipin < g_clbs_nlist.net[netnum].pins.size(); ipin++) {
					fanblk = g_clbs_nlist.net[netnum].pins[ipin].block;
					draw_state->block_color[fanblk] = DRIVES_IT_COLOR;
				}
			}
		} 
		else { /* This net is fanin to the block. */
			if (draw_state->block_color[bnum] == SELECTED_COLOR) {
				/* If block already highlighted, de-highlight the fanin. (the deselect case)*/
				draw_state->net_color[netnum] = BLACK;
				fanblk = g_clbs_nlist.net[netnum].pins[0].block; /* DRIVER to net */
				draw_reset_blk_color(fanblk);
			}
			else {
				/* Highlight the fanin */
				draw_state->net_color[netnum] = DRIVEN_BY_IT_COLOR;
				fanblk = g_clbs_nlist.net[netnum].pins[0].block; /* DRIVER to net */
				draw_state->block_color[fanblk] = DRIVEN_BY_IT_COLOR;
			}
		}
	}

	if (draw_state->block_color[bnum] == SELECTED_COLOR) { 
		/* If block already highlighted, de-highlight the selected block. */
		draw_reset_blk_color(bnum);
	}
	else { 
		/* Highlight the selected block. */
		draw_state->block_color[bnum] = SELECTED_COLOR; 
	}
}


static void deselect_all(void) {
	// Sets the color of all clbs, nets and rr_nodes to the default.
	// as well as clearing the highlighed sub-block


	t_draw_state* draw_state = get_draw_state_vars();
	int i;

	/* Create some colour highlighting */
	for (i = 0; i < num_blocks; i++) {
		draw_reset_blk_color(i);
	}

	for (i = 0; i < (int) g_clbs_nlist.net.size(); i++)
		draw_state->net_color[i] = BLACK;

	for (i = 0; i < num_rr_nodes; i++) {
		draw_state->draw_rr_node[i].color = DEFAULT_RR_NODE_COLOR;
		draw_state->draw_rr_node[i].node_highlighted = false;
	}

	get_selected_sub_block_info().clear();

}


static void draw_reset_blk_color(int i) {

	t_draw_state* draw_state = get_draw_state_vars();

	if (block[i].type->index < 3) {
			draw_state->block_color[i] = LIGHTGREY;
	} else if (block[i].type->index < 3 + MAX_BLOCK_COLOURS) {
			draw_state->block_color[i] = (enum color_types) (BISQUE + MAX_BLOCK_COLOURS + block[i].type->index - 3);
	} else {
			draw_state->block_color[i] = (enum color_types) (BISQUE + 2 * MAX_BLOCK_COLOURS - 1);
	}
}

/**
 * Draws a small triange, at a position along a line from 'start' to 'end'.
 *
 * 'relative_position' [0., 1] defines the triangles position relative to 'start'.
 *
 * A 'relative_position' of 0. draws the triangle centered at 'start'.
 * A 'relative_position' of 1. draws the triangle centered at 'end'.
 * Fractional values draw the triangle along the line
 */
void draw_triangle_along_line(t_point start, t_point end, float relative_position, float arrow_size) {
    VTR_ASSERT(relative_position >= 0. && relative_position <= 1.);
	float xdelta = end.x - start.x;
	float ydelta = end.y - start.y;

    float xtri = start.x + xdelta * relative_position;
    float ytri = start.y + ydelta * relative_position;

	draw_triangle_along_line(xtri, ytri, start.x, end.x, start.y, end.y, arrow_size);
}

/**
 * Draws a trangle with it's center at (xend, yend), and of length & width
 * arrow_size, rotated such that it points in the direction
 * of the directed line segment (x1, y1) -> (x2, y2).
 *
 * Note that the parameters are in a strange order
 */
void draw_triangle_along_line(
	float xend, float yend, float x1, float x2, float y1, float y2, float arrow_size)
{
	float switch_rad = arrow_size/2;
	float xdelta, ydelta;
	float magnitude;
	float xunit, yunit;
	float xbaseline, ybaseline;
	t_point poly[3];

	xdelta = x2 - x1;
	ydelta = y2 - y1;
	magnitude = sqrt(xdelta * xdelta + ydelta * ydelta);

	xunit = xdelta / magnitude;
	yunit = ydelta / magnitude;

	poly[0].x = xend + xunit * switch_rad;
	poly[0].y = yend + yunit * switch_rad;
	xbaseline = xend - xunit * switch_rad;
	ybaseline = yend - yunit * switch_rad;
	poly[1].x = xbaseline + yunit * switch_rad;
	poly[1].y = ybaseline - xunit * switch_rad;
	poly[2].x = xbaseline - yunit * switch_rad;
	poly[2].y = ybaseline + xunit * switch_rad;

	fillpoly(poly, 3);
}

static inline bool LOD_screen_area_test_square(float width, float screen_area_threshold) {

    //Since world coordinates get clipped when converted to screen (at high zoom levels),
    //we can not pick an arbitrary world root coordinate for the rectange we want to test,
    //as clipping could cause it's area to go to zero when we convert from world to screen
    //coordinates.
    //
    //Instead we specify an on-screen location for the rectangle we plan to test
    t_point lower_left = scrn_to_world(t_point(0., 0.)); //Pick one corner of the screen

    //Offset by the width
    t_point upper_right = lower_left;
    upper_right.offset(width, width);

    t_bound_box world_rect = t_bound_box(lower_left, upper_right);

    return LOD_screen_area_test(world_rect, screen_area_threshold);
}

static inline bool default_triangle_LOD_screen_area_test() {
	return triangle_LOD_screen_area_test(DEFAULT_ARROW_SIZE);
}

static inline bool triangle_LOD_screen_area_test(float arrow_size) {
	return LOD_screen_area_test_square(arrow_size*0.66, MIN_VISIBLE_AREA);
}

static void draw_pin_to_chan_edge(int pin_node, int chan_node) {

	/* This routine draws an edge from the pin_node to the chan_node (CHANX or   *
	 * CHANY).  The connection is made to the nearest end of the track instead   *
	 * of perpundicular to the track to symbolize a single-drive connection.     *
	 * If mark_conn is true, draw a box where the pin connects to the track      *
	 * (useful for drawing  the rr graph)                                        */

	/* TODO: Fix this for global routing, currently for detailed only */

	t_draw_coords* draw_coords = get_draw_coords_vars();

	t_rr_type chan_type;
	int grid_x, grid_y, pin_num, chan_xlow, chan_ylow;
	float x1, x2, y1, y2;
	int start, end, i;
	t_bound_box chan_bbox;
	float xend, yend;
	float draw_pin_off;
	enum e_direction direction;
	enum e_side iside;
	t_type_ptr type;

	direction = rr_node[chan_node].get_direction();
	grid_x = rr_node[pin_node].get_xlow();
	grid_y = rr_node[pin_node].get_ylow();
	pin_num = rr_node[pin_node].get_ptc_num();
	chan_type = rr_node[chan_node].type;
	type = grid[grid_x][grid_y].type;

	/* large block begins at primary tile (offset == 0) */
	int width_offset = grid[grid_x][grid_y].width_offset;
	int height_offset = grid[grid_x][grid_y].height_offset;
	grid_x = grid_x - width_offset;
	grid_y = grid_y - height_offset;

	int width = grid[grid_x][grid_y].type->width;
	int height = grid[grid_x][grid_y].type->height;
	chan_ylow = rr_node[chan_node].get_ylow();
	chan_xlow = rr_node[chan_node].get_xlow();

	start = -1;
	end = -1;

	switch (chan_type) {

	case CHANX:
		start = rr_node[chan_node].get_xlow();
		end = rr_node[chan_node].get_xhigh();

		if (is_opin(pin_num, type)) {
			if (direction == INC_DIRECTION) {
				end = rr_node[chan_node].get_xlow();
			} else if (direction == DEC_DIRECTION) {
				start = rr_node[chan_node].get_xhigh();
			}
		}

		start = max(start, grid_x);
		end = min(end, grid_x); /* Width is 1 always */
		VTR_ASSERT(end >= start);
		/* Make sure we are nearby */


		if ((grid_y + height - 1) == chan_ylow) {
			iside = TOP;
			width_offset = width - 1;
			height_offset = height - 1;
			draw_pin_off = draw_coords->pin_size;
		} else if ((grid_y - 1) == chan_ylow) {
			//VTR_ASSERT((grid_y - 1) == chan_ylow);

			iside = BOTTOM;

			width_offset = 0;
			height_offset = 0;

			draw_pin_off = -draw_coords->pin_size;
		}
		else {//This is used to determine where the pins are located in locations
			//other than the perimeter.
				iside=TOP;
				for (int side1 = 0; side1 < 4; ++side1) {
			for (int width1 = 0; width1 < grid[grid_x][grid_y].type->width; ++width1) {
				for (int height1= 0; height1 < grid[grid_x][grid_y].type->height; ++height1) {
					if (grid[grid_x][grid_y].type->pinloc[width1][height1][side1][pin_num])
					{
						height_offset = height1;
						width_offset = width1;
						if(side1==0)
							iside=TOP;
						else if(side1==2) iside=BOTTOM;
					}
				}
			}
		}
			draw_pin_off = -draw_coords->pin_size;
		}

		VTR_ASSERT(grid[grid_x][grid_y].type->pinloc[width_offset][height_offset][iside][pin_num]);

		draw_get_rr_pin_coords(pin_node, iside, width_offset, height_offset, &x1, &y1);
		chan_bbox = draw_get_rr_chan_bbox(chan_node);

		y1 += draw_pin_off;
		y2 = chan_bbox.bottom();
		x2 = x1;
		if (is_opin(pin_num, type)) {
			if (direction == INC_DIRECTION) {
				x2 = chan_bbox.left();
			} else if (direction == DEC_DIRECTION) {
				x2 = chan_bbox.right();
			}
		}
		break;

	case CHANY:
		start = rr_node[chan_node].get_ylow();
		end = rr_node[chan_node].get_yhigh();
		if (is_opin(pin_num, type)) {
			if (direction == INC_DIRECTION) {
				end = rr_node[chan_node].get_ylow();
			} else if (direction == DEC_DIRECTION) {
				start = rr_node[chan_node].get_yhigh();
			}
		}

		start = max(start, grid_y);
		end = min(end, (grid_y + height - 1)); /* Width is 1 always */
		VTR_ASSERT(end >= start);
		/* Make sure we are nearby */

		if ((grid_x) == chan_xlow) {
			iside = RIGHT;
			draw_pin_off = draw_coords->pin_size;
		} else {
			VTR_ASSERT((grid_x - 1) == chan_xlow);
			iside = LEFT;
			draw_pin_off = -draw_coords->pin_size;
		}
		for (i = start; i <= end; i++) {
			height_offset = i - grid_y;
			VTR_ASSERT(height_offset >= 0 && height_offset < type->height);
			/* Once we find the location, break out, this will leave ioff pointing
			 * to the correct offset.  If an offset is not found, the assertion after
			 * this will fail.  With the correct routing graph, the assertion will not
			 * be triggered.  This also takes care of connecting a wire once to multiple
			 * physical pins on the same side. */
			if (grid[grid_x][grid_y].type->pinloc[width_offset][height_offset][iside][pin_num]) {
				break;
			}
		}
		VTR_ASSERT(grid[grid_x][grid_y].type->pinloc[width_offset][height_offset][iside][pin_num]);

		draw_get_rr_pin_coords(pin_node, iside, width_offset, height_offset, &x1, &y1);
		chan_bbox = draw_get_rr_chan_bbox(chan_node);
		
		x1 += draw_pin_off;
		x2 = chan_bbox.left();
		y2 = y1;
		if (is_opin(pin_num, type)) {
			if (direction == INC_DIRECTION) {
				y2 = chan_bbox.bottom();
			} else if (direction == DEC_DIRECTION) {
				y2 = chan_bbox.top();
			}
		}
		break;

	default:
		vpr_throw(VPR_ERROR_OTHER, __FILE__, __LINE__, 
				"in draw_pin_to_chan_edge: Invalid channel node %d.\n", chan_node);
		x1 = x2 = y1 = y2 = OPEN; //Prevents compiler error of variable uninitialized.  
	}

	drawline(x1, y1, x2, y2);

	//don't draw the ex, or triangle unless zoomed in really far
	if (direction == BI_DIRECTION || !is_opin(pin_num, type)) {
		if (LOD_screen_area_test_square(draw_coords->pin_size*1.3,MIN_VISIBLE_AREA) == true) {
			draw_x(x2, y2, 0.7 * draw_coords->pin_size);
		}
	} else {
		if (default_triangle_LOD_screen_area_test() == true) {
			xend = x2 + (x1 - x2) / 10.;
			yend = y2 + (y1 - y2) / 10.;
			draw_triangle_along_line(xend, yend, x1, x2, y1, y2);
		}
	}
}


static void draw_pin_to_pin(int opin_node, int ipin_node) {
	
	/* This routine draws an edge from the opin rr node to the ipin rr node */
	int opin_grid_x, opin_grid_y, opin_pin_num;
	int ipin_grid_x, ipin_grid_y, ipin_pin_num;
	int width_offset, height_offset;
	bool found;
	float x1, x2, y1, y2;
	float xend, yend;
	enum e_side pin_side;
	t_type_ptr type;

	VTR_ASSERT(rr_node[opin_node].type == OPIN);
	VTR_ASSERT(rr_node[ipin_node].type == IPIN);
	x1 = y1 = x2 = y2 = 0;
	width_offset = 0;
	height_offset = 0;
	pin_side = TOP;

	/* get opin coordinate */
	opin_grid_x = rr_node[opin_node].get_xlow();
	opin_grid_y = rr_node[opin_node].get_ylow();
	opin_grid_x = opin_grid_x - grid[opin_grid_x][opin_grid_y].width_offset;
	opin_grid_y = opin_grid_y - grid[opin_grid_x][opin_grid_y].height_offset;

	opin_pin_num = rr_node[opin_node].get_ptc_num();
	type = grid[opin_grid_x][opin_grid_y].type;
	
	found = false;
	for (int width = 0; width < type->width && !found; ++width) {
		for (int height = 0; height < type->height && !found; ++height) {
            for (e_side iside : {TOP, RIGHT, BOTTOM, LEFT}) {

				/* Find first location of pin */
				if (1 == type->pinloc[width][height][iside][opin_pin_num]) {
					width_offset = width;
					height_offset = height;
					pin_side = iside;
					found = true;
				}
			}
		}
	}
	VTR_ASSERT(found);
	draw_get_rr_pin_coords(opin_node, pin_side, width_offset, height_offset, &x1, &y1);

	/* get ipin coordinate */
	ipin_grid_x = rr_node[ipin_node].get_xlow();
	ipin_grid_y = rr_node[ipin_node].get_ylow();
	ipin_grid_x = ipin_grid_x - grid[ipin_grid_x][ipin_grid_y].width_offset;
	ipin_grid_y = ipin_grid_y - grid[ipin_grid_x][ipin_grid_y].height_offset;

	ipin_pin_num = rr_node[ipin_node].get_ptc_num();
	type = grid[ipin_grid_x][ipin_grid_y].type;
	
	found = false;
	for (int width = 0; width < type->width && !found; ++width) {
		for (int height = 0; height < type->height && !found; ++height) {
            for (e_side iside : {TOP, RIGHT, BOTTOM, LEFT}) {
				/* Find first location of pin */
				if (1 == type->pinloc[width][height][iside][ipin_pin_num]) {
					width_offset = width;
					height_offset = height;
					pin_side = iside;
					found = true;
				}
			}
		}
	}
	VTR_ASSERT(found);
	draw_get_rr_pin_coords(ipin_node, pin_side, width_offset, height_offset, &x2, &y2);

	drawline(x1, y1, x2, y2);	
	xend = x2 + (x1 - x2) / 10.;
	yend = y2 + (y1 - y2) / 10.;
	draw_triangle_along_line(xend, yend, x1, x2, y1, y2);
}

static inline void draw_mux_with_size(t_point origin, e_side orientation, float height, int size) {
    setcolor(YELLOW);
    auto bounds = draw_mux(origin, orientation, height);

    setcolor(BLACK);
    drawtext_in(bounds, std::to_string(size));
}

//Draws a mux
static inline t_bound_box draw_mux(t_point origin, e_side orientation, float height) {
    return draw_mux(origin, orientation, height, 0.4*height, 0.6);
}

static inline t_bound_box draw_mux(t_point origin, e_side orientation, float height, float width, float scale) {
    std::array<t_point, 4> mux_polygon;

    switch(orientation) {
        case TOP:
            //Clock-wise from bottom left
            mux_polygon[0] = t_point(origin.x - height / 2, origin.y - width / 2);
            mux_polygon[1] = t_point(origin.x - (scale * height) / 2, origin.y + width / 2);
            mux_polygon[2] = t_point(origin.x + (scale * height) / 2, origin.y + width / 2);
            mux_polygon[3] = t_point(origin.x + height / 2, origin.y - width / 2);
            break;
        case BOTTOM:
            //Clock-wise from bottom left
            mux_polygon[0] = t_point(origin.x - (scale * height) / 2, origin.y - width / 2);
            mux_polygon[1] = t_point(origin.x - height / 2, origin.y + width / 2);
            mux_polygon[2] = t_point(origin.x + height / 2, origin.y + width / 2);
            mux_polygon[3] = t_point(origin.x + (scale * height) / 2, origin.y - width / 2);
            break;
        case LEFT:
            //Clock-wise from bottom left
            mux_polygon[0] = t_point(origin.x - width / 2, origin.y - (scale * height) / 2);
            mux_polygon[1] = t_point(origin.x - width / 2, origin.y + (scale * height) / 2);
            mux_polygon[2] = t_point(origin.x + width / 2, origin.y + height / 2);
            mux_polygon[3] = t_point(origin.x + width / 2, origin.y - height / 2);
            break;
        case RIGHT:
            //Clock-wise from bottom left
            mux_polygon[0] = t_point(origin.x - width / 2, origin.y - height / 2);
            mux_polygon[1] = t_point(origin.x - width / 2, origin.y + height / 2);
            mux_polygon[2] = t_point(origin.x + width / 2, origin.y + (scale * height) / 2);
            mux_polygon[3] = t_point(origin.x + width / 2, origin.y - (scale * height) / 2);
            break;

        default:
            VTR_ASSERT_MSG(false, "Unrecognized orientation");
    }

    fillpoly(mux_polygon.data(), mux_polygon.size());

    t_point min = mux_polygon[0];
    t_point max = mux_polygon[0];
    for(const auto& point : mux_polygon) {
        min.x = std::min(min.x, point.x);
        min.y = std::min(min.y, point.y);
        max.x = std::max(max.x, point.x);
        max.y = std::max(max.y, point.y);
    }

    return t_bound_box(min, max);
}


t_point tnode_draw_coord(tatum::NodeId node) {
    AtomPinId pin = g_atom_lookup.tnode_atom_pin(node);
    return atom_pin_draw_coord(pin);
}

t_point atom_pin_draw_coord(AtomPinId pin) {
    AtomBlockId blk = g_atom_nl.pin_block(pin);
    int clb_index = g_atom_lookup.atom_clb(blk);
    const t_pb_graph_node* pg_gnode = g_atom_lookup.atom_pb_graph_node(blk);

	t_draw_coords* draw_coords = get_draw_coords_vars();
    t_bound_box pb_bbox = draw_coords->get_absolute_pb_bbox(clb_index, pg_gnode);

    //We place each atom pin inside it's pb bounding box
    //and distribute the pins along it's vertical centre line
	const float FRACTION_USABLE_WIDTH = 0.8;
	float width =  pb_bbox.get_width();
	float usable_width =  width  * FRACTION_USABLE_WIDTH;
	float x_offset = pb_bbox.left() + width * (1 - FRACTION_USABLE_WIDTH)/2;

    int pin_index, pin_total;
	find_pin_index_at_model_scope(pin, blk, &pin_index, &pin_total);

	const t_point point =  {
		x_offset + usable_width * pin_index / ((float)pin_total),
		pb_bbox.get_ycenter()
	};

    return point;
}

static void draw_crit_path() {
    tatum::TimingPathCollector path_collector;

    t_draw_state* draw_state = get_draw_state_vars();

    if (draw_state->show_crit_path == DRAW_NO_CRIT_PATH) {
        return;
    }

    if (!draw_state->setup_timing_info) {
        return; //No timing to draw
    }

    //Get the worst timing path
    auto paths = path_collector.collect_worst_setup_paths(*g_timing_graph, *(draw_state->setup_timing_info->setup_analyzer()), 1);
    tatum::TimingPath path = paths[0];

    //Walk through the timing path drawing each edge
    tatum::NodeId prev_node;
    float prev_arr_time = std::numeric_limits<float>::quiet_NaN();
    int i = 0;
    for(tatum::TimingPathElem elem : path.data_arrival_elements()) {
        tatum::NodeId node = elem.node();
        float arr_time = elem.tag().time();

        if(prev_node) {
            float delay = arr_time - prev_arr_time;
            if (draw_state->show_crit_path == DRAW_CRIT_PATH_FLYLINES || draw_state->show_crit_path == DRAW_CRIT_PATH_FLYLINES_DELAYS) {
                setcolor(BLUE);
                setlinestyle(SOLID);
                draw_flyline_timing_edge(tnode_draw_coord(prev_node), tnode_draw_coord(node), delay);
            } else {
                VTR_ASSERT(draw_state->show_crit_path != DRAW_NO_CRIT_PATH);
                //For routed timing edges we draw each 'edge' in a different color, this allows users to identify which routing
                //corresponds to which edge
                //
                //We pick colors from the kelly max-contrast list, for long paths there may be repeates
                t_color color = kelly_max_contrast_colors[i++ % kelly_max_contrast_colors.size()];

                //Draw the routed version of the timing edge
                draw_routed_timing_edge(prev_node, node, delay, color);
            }
        }
        prev_node = node;
        prev_arr_time = arr_time;
    }
}

static void draw_flyline_timing_edge(t_point start, t_point end, float incr_delay) {
    drawline(start, end);
    draw_triangle_along_line(start, end, 0.95, 40*DEFAULT_ARROW_SIZE);
    draw_triangle_along_line(start, end, 0.05, 40*DEFAULT_ARROW_SIZE);


    bool draw_delays = (get_draw_state_vars()->show_crit_path == DRAW_CRIT_PATH_FLYLINES_DELAYS
                        || get_draw_state_vars()->show_crit_path == DRAW_CRIT_PATH_ROUTING_DELAYS);
    if (draw_delays) {
        //Determine the strict bounding box based on the lines start/end
        float min_x = std::min(start.x, end.x);
        float max_x = std::max(start.x, end.x);
        float min_y = std::min(start.y, end.y);
        float max_y = std::max(start.y, end.y);

        //If we have a nearly horizontal/vertical line the bbox is too
        //small to draw the text, so widen it by a tile (i.e. CLB) width
        float tile_width = get_draw_coords_vars()->get_tile_width();
        if (max_x - min_x < tile_width) {
            max_x += tile_width / 2;
            min_x -= tile_width / 2;
        }
        if (max_y - min_y < tile_width) {
            max_y += tile_width / 2;
            min_y -= tile_width / 2;
        }

        //TODO: draw the delays nicer
        //   * rotate to match edge
        //   * offset from line
        //   * track visible in window
        t_bound_box text_bbox(min_x, min_y, max_x, max_y);

        std::stringstream ss;
        ss.precision(3);
        ss << 1e9*incr_delay; //In nanoseconds
        std::string incr_delay_str = ss.str();

        drawtext_in(text_bbox, incr_delay_str.c_str());
    }
}

static void draw_routed_timing_edge(tatum::NodeId start_tnode, tatum::NodeId end_tnode, float incr_delay, t_color color) {

    draw_routed_timing_edge_connection(start_tnode, end_tnode, color);
    
    setlinestyle(DASHED);
    setlinewidth(3);
    setcolor(color);

    draw_flyline_timing_edge(tnode_draw_coord(start_tnode), tnode_draw_coord(end_tnode), incr_delay);

    setlinewidth(0);
    setlinestyle(SOLID);
}

//Collect all the drawing locations associated with the timing edge between start and end
static void draw_routed_timing_edge_connection(tatum::NodeId src_tnode, tatum::NodeId sink_tnode, t_color color) {

    std::vector<t_point> points;

    AtomPinId atom_src_pin = g_atom_lookup.tnode_atom_pin(src_tnode);
    AtomPinId atom_sink_pin = g_atom_lookup.tnode_atom_pin(sink_tnode);

    points.push_back(atom_pin_draw_coord(atom_src_pin));

    tatum::EdgeId tedge = g_timing_graph->find_edge(src_tnode, sink_tnode);
    tatum::EdgeType edge_type = g_timing_graph->edge_type(tedge);

    //We currently only trace interconnect edges in detail, and treat all others
    //as flylines
    if (edge_type == tatum::EdgeType::INTERCONNECT) {
        //All atom pins are implemented inside CLBs, so next hop is to the top-level CLB pins

        //TODO: most of this code is highly similar to code in PostClusterDelayCalculator, refactor
        //      into a common method for walking the clustered netlist, this would also (potentially)
        //      allow us to grab the component delays
        AtomBlockId atom_src_block = g_atom_nl.pin_block(atom_src_pin);
        AtomBlockId atom_sink_block = g_atom_nl.pin_block(atom_sink_pin);

        int clb_src_block = g_atom_lookup.atom_clb(atom_src_block);
        VTR_ASSERT(clb_src_block >= 0);
        int clb_sink_block = g_atom_lookup.atom_clb(atom_sink_block);
        VTR_ASSERT(clb_sink_block >= 0);

        const t_pb_graph_pin* sink_gpin = g_atom_lookup.atom_pin_pb_graph_pin(atom_sink_pin);
        VTR_ASSERT(sink_gpin);

        int sink_pb_route_id = sink_gpin->pin_count_in_cluster;

        const t_net_pin* sink_clb_net_pin = find_pb_route_clb_input_net_pin(clb_sink_block, sink_pb_route_id);
        if(sink_clb_net_pin != nullptr) {
            //Connection leaves the CLB

            int net = sink_clb_net_pin->net;
            const t_net_pin* driver_clb_net_pin = &g_clbs_nlist.net[net].pins[0];
            VTR_ASSERT(driver_clb_net_pin != nullptr);
            VTR_ASSERT(driver_clb_net_pin->block == clb_src_block);

            //Now that we have the CLB source and sink pins, we need to grab all the points on the routing connecting the pins
            auto routed_rr_nodes = trace_routed_connection_rr_nodes(driver_clb_net_pin, sink_clb_net_pin);

            //Mark all the nodes highlighted
            t_draw_state* draw_state = get_draw_state_vars();
            for (int inode : routed_rr_nodes) {
				draw_state->draw_rr_node[inode].color = color;
            }


            draw_partial_route(routed_rr_nodes);
        } else {
            //Connection entirely within the CLB, we don't draw the internal routing so treat it as a fly-line
            VTR_ASSERT(clb_src_block == clb_sink_block);
        }
    }

    points.push_back(atom_pin_draw_coord(atom_sink_pin));
}

//Returns the set of rr nodes which connect driver to sink
static std::vector<int> trace_routed_connection_rr_nodes(const t_net_pin* driver_clb_net_pin, const t_net_pin* sink_clb_net_pin) {
    VTR_ASSERT(driver_clb_net_pin->net == sink_clb_net_pin->net);
    VTR_ASSERT(driver_clb_net_pin->net_pin == 0);

    bool allocated_route_tree_structs = alloc_route_tree_timing_structs(true); //Needed for traceback_to_route_tree

    //Conver the traceback into an easily search-able
    t_rt_node* rt_root = traceback_to_route_tree(driver_clb_net_pin->net);

    VTR_ASSERT(rt_root->inode == net_rr_terminals[driver_clb_net_pin->net][driver_clb_net_pin->net_pin]);

    int sink_rr_node = net_rr_terminals[sink_clb_net_pin->net][sink_clb_net_pin->net_pin];

    std::vector<int> rr_nodes_on_path;

    //Collect the rr nodes
    trace_routed_connection_rr_nodes_recurr(rt_root, sink_rr_node, rr_nodes_on_path);

    //Traced from sink to source, but we want to draw from source to sink
    std::reverse(rr_nodes_on_path.begin(), rr_nodes_on_path.end()); 

    if (allocated_route_tree_structs) {
        free_route_tree_timing_structs(); //Clean-up
    }
    return rr_nodes_on_path;
}

//Helper function for trace_routed_connection_rr_nodes
//Adds the rr nodes linking rt_node to sink_rr_node to rr_nodes_on_path
//Returns true if rt_node is on the path
bool trace_routed_connection_rr_nodes_recurr(const t_rt_node* rt_node, int sink_rr_node, std::vector<int>& rr_nodes_on_path) {
    //DFS from the current rt_node to the sink_rr_node, when the sink is found trace back the used rr nodes

    if (rt_node->inode == sink_rr_node) {
        rr_nodes_on_path.push_back(sink_rr_node);
        return true;
    }

    for (t_linked_rt_edge* edge = rt_node->u.child_list; edge != nullptr; edge = edge->next) {
        t_rt_node* child_rt_node = edge->child;
        VTR_ASSERT(child_rt_node);

        bool on_path_to_sink = trace_routed_connection_rr_nodes_recurr(child_rt_node, sink_rr_node, rr_nodes_on_path);

        if (on_path_to_sink) {
            rr_nodes_on_path.push_back(rt_node->inode);
            return true;
        }
    }

    return false; //Not on path to sink
}

//Find the switch between two rr nodes
static short find_switch(int prev_inode, int inode) {
    for (int i = 0; i < rr_node[prev_inode].get_num_edges(); ++i) {
        if (rr_node[prev_inode].edges[i] == inode) {
            return rr_node[prev_inode].switches[i];
        }
    }
    VTR_ASSERT(false);
    return -1;
}

t_color to_t_color(vtr::Color<float> color) {
    return t_color(color.r*256, color.g*256, color.b*256); 
}
