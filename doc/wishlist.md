# Wishlist

Ideas worth considering that did not go into 0.1. Kept separate on
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

**More languages**
The groundwork is done: every string goes through `gf_str()` and
`locale.library` is opened at startup. A translation is a catalog in
`LOCALE:Catalogs/<language>/` and costs no line of code. Waiting for
someone who wants to make one.
