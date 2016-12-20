# Version 0.9.6

### Gameplay

* Allow using *Alien Weapon Cache*'s temporary military power when settling any world

### GUI

* Avoid selecting more than one card if one is enough
* Disable selecting cards from hand if selecting any is illegal
* Show number of cards drawn during explore in a separate icon
* Fix bug where amount of temporary *Xeno* military needed was not displayed

# Version 0.9.5

### Gameplay

* Regular cards from *Xeno Invasion* expansion added (no invasion game)
* New expansion level: *Rebel vs Imperium* without *The Gathering Storm*
* *Alien Survey Technology* no longer scores with *Galactic Survey: SETI*
* *Alien Oort Cloud Refinery* is now optional to use with *Diversified Economy*

### GUI

* Enhanced action selection: Larger icons and border around selected actions
* Indicate on the prestige icon whether Prestige/Search action is used, and whether Prestige is on the tile
* Bind Shift+Space short cut to "Done" button
* Corrected card image for *Imperium War Faction*
* *AA* indicates *Alien Artifacts* in list of online games
* Do not display tool tips for pay-for-military powers when using *Imperium Supply Convoy*
* Do not display "(or pass if you want to flip a card)" when using *Imperium Supply Convoy*
* Keyboard accelerators also for settle prompt

### Campaigns

* Campaigns for preset starting hands
* Campaigns for perfect starting hands, provided by entranced
* Support player ranges
* Support customizable game options
* Fix bug using Old Earth
* Fix problems for cards with duplicate names
* Add `DRAW_FOUR` flag (for preset start hands)
* Add description at top of `campaign.txt`
* Support campaign separators (names starting with `===` or `---`)
* Support custom cards

# Changelog for 0.9.4 versions

## 0.9.4q

* Fix crash when campaign key was not present in preferences file

## 0.9.4p

### Bugs

* Fix crashes when taking over using *IL*
* Fix problems during payment when having *Galactic Scavengers*
* Fix instances where log format from server was lost
* Try to avoid weird bug where game begins over and over again

### UI

* Find forced goods while consuming
* Only make useful cards in tableau selectable
* Disable menu items while loading or replaying game
* Don't write VPs in takeover tooltips if "VP in hand" is off
* Compute military correctly is message box when attacking online with *Rebel Alliance*/*Sneak Attack*
* Only display cost information in message box if enabled in options

### New Game dialog

* Campaign selection moved to new game dialog
* Group Advanced, Goals and Takeover check boxes into "Game Options" group

### Online

* Autocompletion in server text box
* Overhaul of the online "Create Game" dialog
* Sortable games and users in lobby
* Bolding own user and game in lobby
* Change sensitivity of "Join Game" button depending on selected game
* Change selected "Number of players" if value is too large

### Logs

* Colored log always on
* Add "=== Game information ===" header at start and end of game
* Add world name to discard to produce message
* Change "draws" to "flips" in *Gambling World* log, and added verbose logs whether card is kept or discarded
* Put back "(Debug game.)" message, and add it for loaded and server games as well
* Switch order of Prestige leader message and bonus draw message
* Correct timing of reshuffle message
* Change timing of some draw and prestige messages
* More verbose messages during consumption
* Log number of drawn cards during Explore (verbose)

### Tool tip

* More card names and minor formatting in settle discount tool tip
* Remove impossible payments from settle tool tip
* Minor formatting in placement and discount tool tips
* Add potential discount to discount tool tip
* Add more "reduce to 0" tool tips

### Debug

* Debug shuffle/rotate and take card/VP/prestige menu items (commented out by default)
* Update display and perform auto save after debug choices
* Disable debug menu items when game is over

### Misc

* Export card locations (optional)
* Specify campaign name from command line
* Never kick AI players
* Check for aborted game after `settle_finish`
* Activate export even when waiting for other players
* Customizable database user and password in server executable

---

# Changelog for 0.8.1 versions

All changes listed here were incorporated into the official 0.9.4 version.

## 0.8.1m

### User interface

* Fix bug when trying to perform a Takeover having *Imperium Fuel Depot*
* Update game state when reconnecting to an online game
* Disallow performing Search and Prestige action at the same time
* Display [AI] before online player names if they are controlled by the AI
* Don't add takeover pass information when using *Rebel Sneak Attack*
* Print seed even if it is 0

### Export/Save/Load

* Export the name of the player doing the export
* Export whether players are played by the AI

### Server

* Resend optional play message if client reconnects
* Remove server verification of choices (it was too buggy)
* Customizable timeout for unstarted games
* Save result to database immediately after the game has finished
* Save waiting status in database
* Send whether Prestige is on the tile and whether a player is controlled by the AI
* Read additional server welcome messages from an optional file
* Enhanced server logging
* Remove prepare icon when player is replaced by AI

### Misc

* Reduce memory footprint
* Load the new icons from installation directory, if installed
* Try to load images from file before loading from `images.data`
* Support for additional cards in `cards.txt`

## 0.8.1l

### User interface

* Move phase summary to the left and use icons instead of text
* Auto-select forced choices for payment/consume/produce (optional)
* Add pass information for takeover during settle choice
* Much larger maximum log width
* Reset card image for each game
* Clear phases when game is over
* Update DONE sensitivity when using `Shift+F12`
* Display "Waiting for server" when waiting for server
* Display "Waiting for opponents" also in offline games
* Show action icons during opponent's search phase in online games
* Use singular "Waiting for opponent" in two player games
* Show a dialog with error message before the client crashes

### Tool tips

* Check for legal target when computing takeover tool tips
* Display potential temporary military in tool tip
* Keep "Additional attack when using ..." in tool tip even when power is used
* Removed duplicate "activated" and "additional" temporary military notes on newer servers

### Logging

* Log start world draws
* Put debug card messages in log
* Slight change in timing of *IL*/Takeover log messages

### Debug card dialog

* Enable save/undo/redo after using the "Debug card" dialog
* Enable the "Debug card" dialog when playing on debug servers
* Add a cancel button to "Debug card" dialog
* Do not instantly update moved cards anymore
* Consistent order on debug cards

### Export/Save/Load

* New "Advanced options" dialog, with autosave and export options
* Export game when finished (optional and instead of save log at end of game)
* Include save game in export
* Load from (offline) exported games
* Export server information and tampered state
* Configurable XLST style sheet for exported games
* Recognize server chat messages in export
* Don't allow loading games from before version `0.8.0`

### Server

* Kick old player and use new connection if a logged in player logs in again
* Simple server verification of choices
* Send ping even when timeout is disabled
* Debug flag (`-debug`) to accept "Debug card" dialog choices
* Timestamps on server logs
* More information of player state in server log
* Server parameters to specify export file location (`-e`), style sheet (`-ss`) and server name (`-s`)
* Usage information (`-h`)

### Misc

* Unify source code across platforms

## 0.8.1k

### Tool tips

* Cost information during placement (optional)
* Goal description in goal tool tips
* List trade values on goods during trade
* Takeover tool tip depends on takeover power
* Temporary settle discount in discount tool tip
* *ICT*/*IIF* information in discount tool tip

### User interface

* Cost information during payment
* Right-click also for deselecting all cards
* Accelerators for Disconnect and Resign menu items
* GUI option to always display key cues
* Sort draw produce powers last
* Interactive search in debug dialog
* Deactivate "Add AI player" if game is full
* "Extra strength" -> "Extra military"
* "? VP" -> "? VPs"
* "Choose goods to consume" -> "Choose good to consume" when only one good needed

### Logging

* Log draws from under *Galactic Scavengers*
* Log card draws (optional)
* Log discarded start world

### Misc

* Regenerate random seed whenever a new game is started
* Send release information to server
* Game round is 0 during start world selection

### Server changes

* Save messages in database and replay to reconnected clients
* Export game when completed
* Added timeout parameters (`0` to disable)
* Encrypt passwords in database
* Remember client version
* Make online AI remember saved cards

## 0.8.1j

* Sort produce powers
* Enhanced consume powers sorting
* Display VP diffs for worlds that can be taken over
* Display VP from goals in hand tool tip (assuming opponents do not place any cards)
* `F12`/`Shift+F12` selects/deselects all hand cards
* Right-click card to select all others
* More instances where only one card can be selected (like trading)
* Grey out table cards when defending a takeover
* Show accelerator reminders
* Verbose log when card is saved
* Log total tie breaker (and add "VPs" to final score messages)
* Do not send empty chat messages
* Select online game after it has been created
* Export start world and *Gambling World* choices
* Don't allow entering of too long game descriptions or passwords
* Don't resign the game if escape is pressed in the resign dialog
* Don't log *AOCR*'s kind if it is in hand at game end
* More visible errors during initialization
* Log obscure cases where an AI activates temporary military for free

## 0.8.1i

### Game interface

* Graphical draw, discard and VP status
* Display negative VPs in status
* Display cards from log
* Display VP score for 6-cost developments in card tool tip
* Display VP score for cards in hand (optional)
* Military icon is not shown if there is nothing special to see
* VP tool tip:
  - Display breakdown of score
* Military tool tip:
  - Specialized strength now relative (instead of absolute)
  - Added takeover defense and **IMPERIUM** attack strength
  - Do not write **REBEL**/**IMPERIUM** in military tool tip when takeovers are disabled
* New Prestige tool tip:
  - Display whether a player has used his Search/Prestige action
  - Display whether the Prestige Leader has Prestige on tile or not
* Settle discount icon (optional), with tool tip:
  - General settle discount
  - Specialized settle discount
  - Whether player can discard to place at 0 cost
  - Pay-for-military powers
* Goal tool tip:
  - Report progress for some first goals (local games only)
* Add Prestige icon to action buttons when deciding action in non-advanced mode

### User interface

* Enhanced keyboard support:
  - `F1`-`F9` and `Shift+F1`-`Shift+F9`: Activate selectable cards in tableaux and hand
  - `F1`-`F7` and `Shift+F1`-`Shift+F4`: Actions
  - `Shift+Enter`: 'Done' button
  - `Shift+Up/Down`: Change selection in drop down
  - `F12`: Open selection drop down
  - Numeric keys (`1`-`9` and `0`) removed for online play
  - Lobby shortcuts (`Ctrl+R`, `Ctrl+J`, `Ctrl+S` etc)
* Short cuts for menu items
* Menu items disabled if they do not have any effect
* Graphical error messages if crashing during startup
* Remember save location
* Hitting enter in text input will accept dialogs
* Disable Connect button when trying to connect to server
* Clear lobby messages when changing server
* Export game state to XML

### Undo/Redo

* A separate Undo menu
* Redo choice (if previously undone)
* Undo/redo round
* Undo/redo game
* Undo in saved games
* Replay game

### Message log ~~(local games only)~~

* Write welcome message and settings at start of game
* Log phases ("--- Explore phase ---")
* Log military attack and defense strength during takeover
* Changed timing of messages during placement and paying for cards
* Print end score (and tie breaker if needed) for all players
* Print seed at end of game
* Bolded start of round and "Refreshing draw deck" message
* Colored log (optional)
  - Yellow for goals
  - Red for takeover notices
  - Purple for Search/Prestige action and Prestige bonuses
  - Blue for start of phases
* Verbose messages (optional)
  - Note first player
  - When not placing a card
  - When drawing cards
  - When gaining Prestige
  - When discarding for "Draw then discard" powers
  - When changing *Alien Oort Cloud Refinery*'s kind
* Log discards and saved cards (optional)
* Some other minor enhancements (esp for *IL*, *GS* and takeovers)

### Interface options

* Immediate preview of changed GUI parameters
* Resize preview card continuously
* Customizable log width
* Settle discount icon on/off
* Colored log on/off
* Verbose log on/off
* Log discards on/off
* Autosave and -restore on/off
* Save message log after each game on/off
* Configurable auto save and log location
* When starting a new game, use previous options

### Game parameters

* "Select Parameters..." -> "New..."
* Set name of human player
* Disable illegal player numbers when selecting expansions
* Specify game seed

### Command line parameters

* `-n`: Set the name of the human player
* `-s`: Load from save game
* `-g`/`-nog`: Enable/Disable goal
* `-t`/`-not`: Enable/Disable takeovers

### Debug functionality

* Sortable cards in debug dialog
* Update cards instantly when Debug value changes
* Disable undo/redo/save after moving cards in debug (to avoid crashing)

### String changes

* "Draw per type produced" -> "Draw per kind produced"
* "6-cost development" -> "6-cost development giving ? VPs"
* "Gene" -> "Genes"
* *Universal Symbionts*' power now says "Produce on another Genes windfall"
* "--- Round X begins ---" -> "=== Round X begins ==="
* Consistent pluralization of VPs and cards

### Bug fixes

* Fix *Psi-Crystal World* second phase display bug
* Fix crashes when loading new game during some player selections
* Toggle Prestige also when Search and another action is selected
* Do not crash if disconnecting after online game just has ended
* Do not create game if "Create game" dialog is closed by escape
* Disable "Add AI Player" when leaving a created game
* Rejected login now disconnects from server
* Do not crash if user presses escape while connecting
* Disable some dialogs while waiting for opponent (to avoid unresponsiveness)
* Don't lose tool tips when resizing window
* Always disable action button when game is over
* Fixed crash when moving debug from (None, Active)
