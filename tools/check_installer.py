#!/usr/bin/env python3
"""
Controleert het Installer-script op fouten die anders pas op de Amiga
opvallen.

Het script is hier niet te draaien -- daar is de Installer van AmigaOS
voor nodig -- maar de fouten die het tot nu toe bevatte waren allemaal
van een soort die je zonder uitvoeren kunt vinden: haakjes uit balans, en
opties binnen copyfiles die elkaar uitsluiten. Die kosten anders een
testronde op echte hardware per stuk.
"""

import re
import sys


def strip_comments_and_strings(text):
    """Vervangt commentaar en stringinhoud door spaties, met behoud van
    de regelindeling, zodat regelnummers blijven kloppen."""
    out = []
    in_string = False
    i = 0
    while i < len(text):
        c = text[i]
        if c == '\n':
            out.append(c)
            i += 1
        elif c == ';' and not in_string:
            while i < len(text) and text[i] != '\n':
                out.append(' ')
                i += 1
        elif c == '"':
            in_string = not in_string
            out.append(' ')
            i += 1
        elif in_string:
            out.append(' ')
            i += 1
        else:
            out.append(c)
            i += 1
    return ''.join(out)


def check(path):
    raw = open(path).read()
    code = strip_comments_and_strings(raw)
    problems = []

    # 1. haakjes
    depth = 0
    line = 1
    for c in code:
        if c == '\n':
            line += 1
        elif c == '(':
            depth += 1
        elif c == ')':
            depth -= 1
            if depth < 0:
                problems.append('regel %d: sluithaakje te veel' % line)
                depth = 0
    if depth > 0:
        problems.append('einde: %d sluithaakje(s) tekort' % depth)

    # 2. copyfiles: (all), (pattern) en (choices) sluiten elkaar uit
    for m in re.finditer(r'\(copyfiles\b', code):
        start = m.start()
        d = 0
        end = start
        for j in range(start, len(code)):
            if code[j] == '(':
                d += 1
            elif code[j] == ')':
                d -= 1
                if d == 0:
                    end = j
                    break
        body = code[start:end]
        used = [o for o in ('all', 'pattern', 'choices')
                if re.search(r'\(%s\b' % o, body)]
        if len(used) > 1:
            ln = code[:start].count('\n') + 1
            problems.append('regel %d: copyfiles gebruikt %s; die sluiten '
                            'elkaar uit' % (ln, ' en '.join(used)))

    # 3. commando's die de Installer niet kent
    known = {
        'abort', 'all', 'askbool', 'askchoice', 'askdir', 'askfile',
        'asknumber', 'askoptions', 'askstring', 'cat', 'choices', 'complete',
        'confirm', 'copyfiles', 'copylib', 'database', 'debug', 'default',
        'delete', 'delopts', 'dest', 'earlier', 'exists', 'exit', 'expandpath',
        'fileonly', 'files', 'foreach', 'getassign', 'getdevice', 'getdiskspace',
        'getenv', 'getsize', 'getsum', 'getversion', 'help', 'if', 'infos',
        'makeassign', 'makedir', 'message', 'newname', 'newpath', 'not',
        'or', 'and', 'pathonly', 'pattern', 'procedure', 'prompt', 'protect',
        'quiet', 'rename', 'run', 'select', 'set', 'setdefaulttool', 'settooltype',
        'startup', 'strlen', 'substr', 'symbolset', 'tackon', 'textfile',
        'tooltype', 'trap', 'until', 'user', 'welcome', 'while', 'working',
        'nogauge', 'safe', 'optional', 'source', 'swapcolors', 'transcript',
    }
    for m in re.finditer(r'\((\w[\w-]*)', code):
        name = m.group(1)
        if name not in known and not name.isdigit():
            ln = code[:m.start()].count('\n') + 1
            problems.append('regel %d: onbekend commando (%s' % (ln, name))

    return problems


if __name__ == '__main__':
    paths = sys.argv[1:] or ['pkg/Install']
    failed = False
    for p in paths:
        problems = check(p)
        if problems:
            failed = True
            print('%s:' % p)
            for pr in problems:
                print('  %s' % pr)
        else:
            print('%s: geen problemen gevonden' % p)
    sys.exit(1 if failed else 0)
