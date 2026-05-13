#!/usr/bin/env python3
"""Validate repository agent skill metadata."""

from pathlib import Path
import argparse
import re
import sys


REQUIRED_FIELDS = ("name", "description")
OPTIONAL_FIELDS = ("license", "compatibility", "metadata", "allowed-tools")
ALLOWED_FIELDS = set(REQUIRED_FIELDS + OPTIONAL_FIELDS)
SKILL_NAME_RE = re.compile(r"^[a-z0-9]+(?:-[a-z0-9]+)*$")
MAX_NAME_LEN = 64
MAX_DESCRIPTION_LEN = 1024
MAX_COMPATIBILITY_LEN = 500


def parse_frontmatter(path):
    lines = path.read_text(encoding="utf-8").splitlines()
    if not lines or lines[0] != "---":
        raise ValueError("missing opening frontmatter marker")

    fields = {}
    current_key = None
    for lineno, line in enumerate(lines[1:], start=2):
        if line == "---":
            return fields, lines[lineno:]
        if not line.strip() or line.lstrip().startswith("#"):
            continue
        if line[0].isspace():
            if current_key is None:
                raise ValueError(
                    f"nested frontmatter value without parent at line {lineno}")
            nested = line.strip()
            current_value = fields[current_key]
            fields[current_key] = (
                f"{current_value}\n{nested}" if current_value else nested)
            continue
        if ":" not in line:
            raise ValueError(f"invalid frontmatter line {lineno}: {line}")
        key, value = line.split(":", 1)
        current_key = key.strip()
        fields[current_key] = value.strip().strip("'\"")

    raise ValueError("missing closing frontmatter marker")


def validate_skill(path):
    try:
        fields, body = parse_frontmatter(path)
    except ValueError as err:
        return [str(err)]

    errors = []
    for field in REQUIRED_FIELDS:
        if not fields.get(field):
            errors.append(f"missing `{field}`")

    for field in fields:
        if field not in ALLOWED_FIELDS:
            errors.append(f"unknown frontmatter field `{field}`")

    name = fields.get("name", "")
    if name:
        if len(name) > MAX_NAME_LEN:
            errors.append(f"`name` must be {MAX_NAME_LEN} characters or fewer")
        if not SKILL_NAME_RE.fullmatch(name):
            errors.append(
                "`name` must use lowercase words separated by single hyphens")

    expected_name = path.parent.name
    if name and name != expected_name:
        errors.append(f"`name` must match directory `{expected_name}`")

    description = fields.get("description", "")
    if description:
        if len(description) > MAX_DESCRIPTION_LEN:
            errors.append("`description` must be 1024 characters or fewer")

    compatibility = fields.get("compatibility", "")
    if compatibility and len(compatibility) > MAX_COMPATIBILITY_LEN:
        errors.append("`compatibility` must be 500 characters or fewer")

    for optional_field in ("license", "allowed-tools"):
        if optional_field in fields and not fields[optional_field]:
            errors.append(f"`{optional_field}` must be non-empty when present")

    if not any(line.strip() for line in body):
        errors.append("missing markdown body")

    return errors


def collect_skill_files(root):
    if not root.exists():
        return [], [f"{root}: directory does not exist"]
    if not root.is_dir():
        return [], [f"{root}: not a directory"]

    skill_files = []
    errors = []
    for child in sorted(root.iterdir()):
        if not child.is_dir():
            continue
        skill_file = child / "SKILL.md"
        if not skill_file.exists():
            errors.append(f"{child}: missing SKILL.md")
            continue
        skill_files.append(skill_file)

    if not skill_files:
        errors.append(f"{root}: no skill files found")

    return skill_files, errors


def main():
    parser = argparse.ArgumentParser(
        description="Validate .agents skill metadata.")
    parser.add_argument(
        "root",
        nargs="?",
        default=".agents/skills",
        help="skill root directory (default: .agents/skills)")
    args = parser.parse_args()

    root = Path(args.root)
    skill_files, errors = collect_skill_files(root)

    names = set()
    for path in skill_files:
        skill_errors = validate_skill(path)
        if not skill_errors:
            fields, _body = parse_frontmatter(path)
            name = fields.get("name")
            if name in names:
                skill_errors.append(f"duplicate skill name `{name}`")
            names.add(name)

        for error in skill_errors:
            errors.append(f"{path}: {error}")

    if errors:
        for error in errors:
            print(error, file=sys.stderr)
        return 1

    print(f"validated {len(skill_files)} skill file(s)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
