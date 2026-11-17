#!/usr/bin/env python3
"""Snapshot player-visible command help from the owned server and quest registries."""
import argparse
import json
import pathlib
import re

p = argparse.ArgumentParser()
p.add_argument('--server', type=pathlib.Path, required=True)
p.add_argument('--quests', type=pathlib.Path, required=True)
p.add_argument('--out', type=pathlib.Path, required=True)
a = p.parse_args()
entries = {}
for name, description, access in re.findall(r'command_add\("([^"\n]+)",\s*"((?:\\.|[^"\\])*)",\s*AccountStatus::(\w+)', a.server.read_text(encoding='utf-8')):
    description = json.loads('"' + description.replace('\t', '\\t') + '"')
    entries[name] = (description, 'Player' if access == 'Player' else access, 'Server')
for name, access, module in re.findall(r'commands\["([^"\n]+)"\]\s*=\s*\{\s*(\d+),\s*require\(commands_path\s*\.\.\s*"([^"\n]+)"', a.quests.read_text(encoding='utf-8')):
    entries[name] = (f'{module.replace("_", " ").title()} commands; use #{name} help for options.', 'Player' if access == '0' else f'Staff {access}', 'Bastion')
if len(entries) < 200:
    raise SystemExit('Unexpectedly incomplete command registry')
lines = ['// Player-visible help snapshot. Regenerate with tools/generate_commands.py.']
for name, (description, access, group) in sorted(entries.items()):
    lines.append('{' + ','.join(json.dumps(s, ensure_ascii=True) for s in (name, description, access, group)) + '},')
a.out.write_text('\n'.join(lines) + '\n', encoding='utf-8')
print(f'{len(entries)} registered server commands')
