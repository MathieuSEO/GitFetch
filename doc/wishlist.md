# Wishlist

Ideas worth considering that did not go into 0.1 or 0.2. Kept separate on
purpose: the program has to stay small. On an Amiga with 2 MB every
kilobyte counts, and a feature nobody uses still costs memory for
everyone.

Each entry says roughly what it would cost in code, because here that is
as much part of the decision as the benefit.

## Considered for a next version

**Running on OS 3.0 to 3.4 through ClassAct**
ReAction grew out of ClassAct and uses the same class names and tags. The
ClassAct classes report as version 41/42, however, while `open_classes()`
demands version 44 — and that is where it stops. The five classes GitFetch
really needs (window, layout, button, string, listbrowser) are confirmed
present in ClassAct 2.0, with the tags used here.

The proposal is to set the version requirement per class: zero for those
five, and keep 44 for the optional ones (chooser, getfile, fuelgauge).
That last part is not a detail: `AllocChooserNode` is a library *function*,
and calling it on an older chooser is a jump into nothing. Unknown *tags*
are harmlessly ignored; missing functions are not.

Cost: a few lines, no meaningful bytes. Does need a test round under
emulation with OS 3.1 and the ClassAct classes; not everything could be
confirmed in advance, in particular the menu and the double-click.

**Running on a stock OS 2.04 to 3.1, with GadTools**
The other route to older systems, and the one MintPRINT took: its
settings program uses only GadTools (buttons, checkboxes, cycle gadgets,
listviews, string and text gadgets), which every Amiga since 2.04 has in
ROM. Nothing extra to install, unlike ClassAct.

What GadTools lacks is what GitFetch leans on: a list with columns, a
progress bar, a layout engine that follows the font, and a file requester
button. Columns become fixed-width text in a listview, the bar is drawn by
hand, the layout is computed from the font size, and the requester is a
button that opens ASL. The full list of gadget kinds is in the NDK, in
`libraries/gadtools.h` (`BUTTON_KIND` and friends).

Cost: large. `gui.c` and `prefswin.c` (about 2000 lines) would be written
again, or kept twice. Worth it only if the ClassAct route above turns out
to leave people out who would otherwise use GitFetch.

**A progress bar in the Herald notification**
Herald can show a bar with Stop and Pause buttons for a running job. The
bar alone is a matter of sending updates; the buttons send their command
back to an ARexx port of the program's own, and GitFetch has none (see
below). Cost: small for the bar, medium with the buttons.

**Only one GitFetch at a time**
`GitFetch owner/repo` from a browser now starts a second copy when one is
already open. Handing the repository to the running one needs a public
message port. Cost: small, but it would be the first step towards an
ARexx port, which is listed below as deliberately left out.

**Transfer speed while downloading (KB/s)**
The numbers are already there: `sofar` and the start time. On a slow line
there is no way to tell "slow" from "stuck", which worries people
needlessly. Cost: small, a few hundred bytes.

**Remember the last repository used**
Put the last address straight into the field at startup. Cost: small,
fits the existing preferences.

**Sortable columns**
`LISTBROWSER_TitleClickable` already exists in the class; it is mostly the
sorting logic that comes with it. The benefit is limited: releases already
arrive newest first. Cost: medium.

**Filtering the file list**
Useful for releases with dozens of files, which is rare for Amiga
software. Cost: medium.

**More languages**
English, Dutch and German since 0.2. Another language costs no line of code: copy
`catalogs/nederlands.ct`, translate it, and `make catalogs` checks and
builds it. Waiting for someone who wants to make one.

## Deliberately not

**Unpacking and installing after the download**
Left outside the scope: GitFetch fetches, and the rest you do with the
tools you already trust. It also keeps xadmaster or lha off the
dependency list.

**An NTP client to set the clock**
Setting the clock is a system-wide job, and there are good programs for it
(Roadshow has one built in, and SNTP is on Aminet). Two programs reaching
for the same clock causes more trouble than it solves. GitFetch does check
the date and says so when it looks wrong.

**An ARexx port**
Customary on AmigaOS for scriptability, but it needs a command table, a
port of its own, and documentation. At version 0.1 with a handful of users
that does not weigh up against the size. Cost: large.
