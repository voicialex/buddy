#!/bin/bash
set -euo pipefail

# Code formatting and linting script for src/ directory
# Uses: clang-format, cmake-format, cpplint
#
# Usage:
#   ./format_and_lint.sh          # Format + lint
#   ./format_and_lint.sh --check  # Dry-run (exit 1 if unformatted)

readonly TARGET_DIR="src"
readonly CHECK_ONLY="${1:---format}"  # --check or --format

# -----------------------------------------------------------------------------
# Colors
# -----------------------------------------------------------------------------
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m'
BOLD='\033[1m'

print_sep() { echo -e "${BOLD}${CYAN}==========================================${NC}"; }

print_sep
echo -e "${BOLD}${CYAN}Code Formatting and Linting Tool${NC}"
echo -e "${CYAN}Target: ${TARGET_DIR}/${NC}"
print_sep
echo ""

# -----------------------------------------------------------------------------
# Tool checks (fail-fast, never auto-install)
# -----------------------------------------------------------------------------
missing=()
for tool in clang-format cmake-format; do
  command -v "$tool" &>/dev/null || missing+=("$tool")
done
if (( ${#missing[@]} )); then
  echo -e "${RED}Missing tools: ${missing[*]}${NC}"
  echo -e "${YELLOW}Install: pip install ${missing[*]}${NC}"
  exit 1
fi

# -----------------------------------------------------------------------------
# Generic formatter: tool_cmd file → stdout
# -----------------------------------------------------------------------------
run_formatter() {
  local title="$1"
  shift
  local tool_cmd=("$@")  # remaining args before --
  shift $#

  # Collect file list from stdin
  local files=()
  while IFS= read -r f; do
    [[ -f "$f" ]] && files+=("$f")
  done

  echo -e "${BOLD}${BLUE}${title}${NC}"
  if (( ${#files[@]} == 0 )); then
    echo -e "${YELLOW}  No files found.${NC}"
    echo ""
    return 0
  fi
  echo -e "${CYAN}  Files: ${#files[@]}${NC}"

  local formatted=0 passed=0 errors=0
  for file in "${files[@]}"; do
    local tmp
    tmp=$(mktemp)
    if "${tool_cmd[@]}" "$file" > "$tmp" 2>/dev/null; then
      if ! cmp -s "$file" "$tmp"; then
        if [[ "$CHECK_ONLY" == "--check" ]]; then
          echo -e "  ${RED}✗${NC} $file"
          ((errors++))
        else
          mv "$tmp" "$file"
          echo -e "  ${GREEN}✓${NC} $file"
          ((formatted++))
          continue
        fi
      else
        ((passed++))
      fi
    else
      echo -e "  ${RED}✗${NC} $file (tool error)"
      ((errors++))
    fi
    rm -f "$tmp"
  done

  (( formatted )) && echo -e "${GREEN}  Formatted: $formatted${NC}"
  (( passed ))    && echo -e "${GREEN}  Already clean: $passed${NC}"
  (( errors ))    && echo -e "${RED}  Errors: $errors${NC}"
  echo ""
  return $errors
}

# -----------------------------------------------------------------------------
# Format C++
# -----------------------------------------------------------------------------
format_cpp() {
  find "$TARGET_DIR" -type f \( -name '*.cpp' -o -name '*.cc' -o -name '*.h' -o -name '*.hpp' \) \
    | run_formatter "Formatting C++..." clang-format --style=file
}

# -----------------------------------------------------------------------------
# Format CMake
# -----------------------------------------------------------------------------
format_cmake() {
  find "$TARGET_DIR" -type f \( -name 'CMakeLists.txt' -o -name '*.cmake' \) \
    | run_formatter "Formatting CMake..." cmake-format
}

# -----------------------------------------------------------------------------
# cpplint
# -----------------------------------------------------------------------------
run_cpplint() {
  echo -e "${BOLD}${BLUE}Running cpplint...${NC}"

  local -a filter_rules=()
  if [[ -f .arclint ]]; then
    local ll
    ll=$(grep -oP '"--linelength=\K[0-9]+' .arclint | head -1)
  fi
  local linelength="${ll:-120}"

  # Collect disabled rules from .arclint
  local disabled=()
  local rules=(
    "build/c++11" "build/include_order" "build/include_what_you_use"
    "build/include_subdir" "runtime/references" "legal/copyright"
    "whitespace/line_length" "whitespace/indent_namespace"
    "whitespace/indent" "readability/todo"
  )
  for rule in "${rules[@]}"; do
    if [[ -f .arclint ]] && grep -q "\"$rule\": \"disabled\"" .arclint 2>/dev/null; then
      disabled+=("-$rule")
    fi
  done

  local filter_arg=""
  if (( ${#disabled[@]} )); then
    filter_arg=$(IFS=','; echo "${disabled[*]}")
  fi

  # Find files
  local -a files
  mapfile -t files < <(find "$TARGET_DIR" -type f \( -name '*.cpp' -o -name '*.cc' -o -name '*.h' -o -name '*.hpp' \))

  if (( ${#files[@]} == 0 )); then
    echo -e "${YELLOW}  No C++ files found.${NC}"
    return 0
  fi
  echo -e "${CYAN}  Files: ${#files[@]}, linelength=$linelength${NC}"

  local -a cmd=(cpplint --counting=detailed "--linelength=$linelength")
  [[ -n "$filter_arg" ]] && cmd+=("--filter=$filter_arg")

  local output rc=0
  output=$("${cmd[@]}" "${files[@]}" 2>&1) || rc=$?

  if (( rc == 0 )); then
    echo -e "${GREEN}  cpplint passed!${NC}"
  else
    local cnt
    cnt=$(echo "$output" | grep -oP 'Total errors found: \K\d+' | tail -1)
    echo -e "${RED}  cpplint: ${cnt:-?} issue(s)${NC}"
    echo "$output" | grep -E ':[0-9]+:' | head -80
    echo ""
  fi
  echo ""
  return $rc
}

# -----------------------------------------------------------------------------
# Main
# -----------------------------------------------------------------------------
exit_code=0

format_cpp   || exit_code=1
format_cmake || exit_code=1
# run_cpplint  || exit_code=1  # TODO: enable when cpplint rules are finalized

print_sep
if (( exit_code == 0 )); then
  echo -e "${BOLD}${GREEN}All checks passed!${NC}"
else
  echo -e "${BOLD}${YELLOW}Completed with issues.${NC}"
fi
print_sep

exit $exit_code
