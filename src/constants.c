/*
 * Race for the Galaxy AI
 * 
 * Copyright (C) 2009-2011 Keldon Jones
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

#include "rftg.h"

/*
 * Action names.
 */
char *actname[MAX_ACTION * 2 - 1] =
{
	"Search",
	"Explore +5",
	"Explore +1,+1",
	"Develop",
	"Develop",
	"Settle",
	"Settle",
	"Consume-Trade",
	"Consume-x2",
	"Produce",
	"Prestige Explore +5",
	"Prestige Explore +1,+1",
	"Prestige Develop",
	"Prestige Develop",
	"Prestige Settle",
	"Prestige Settle",
	"Prestige Consume-Trade",
	"Prestige Consume-x2",
	"Prestige Produce",
};

/*
 * Goal names.
 */
char *goal_name[MAX_GOAL] =
{
	"Galactic Standard of Living",
	"System Diversity",
	"Overlord Discoveries",
	"Budget Surplus",
	"Innovation Leader",
	"Galactic Status",
	"Uplift Knowledge",
	"Galactic Riches",
	"Expansion Leader",
	"Peace/War Leader",
	"Galactic Standing",
	"Military Influence",

	"Greatest Military",
	"Largest Industry",
	"Greatest Infrastructure",
	"Production Leader",
	"Research Leader",
	"Propaganda Edge",
	"Galactic Prestige",
	"Prosperity Lead",
};

/*
 * Names of Search categories.
 */
char *search_name[MAX_SEARCH] =
{
	"development providing +1 or +2 Military",
	"military windfall with 1 or 2 defense",
	"peaceful windfall with 1 or 2 cost",
	"world with Chromosome symbol",
	"world producing or coming with Alien good",
	"card consuming two or more goods",
	"military world with 5 or more defense",
	"6-cost development giving ? VP",
	"card with takeover power"
};

/*
 * Expansion level names.
 */
char *exp_names[MAX_EXPANSION + 1] =
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
char *player_labels[MAX_PLAYER] =
{
	"Two players",
	"Three players",
	"Four players",
	"Five players",
	"Six players",
	NULL
};
