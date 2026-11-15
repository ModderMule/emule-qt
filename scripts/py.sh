# Sourced by shell scripts: py = Python of the root uv project, python3 without uv (CI containers).
# Usage: . "$(dirname "$0")/py.sh"; py script.py args...
_PY_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
py() {
    if command -v uv > /dev/null 2>&1 && [[ -f "$_PY_ROOT/pyproject.toml" ]]; then
        uv run --quiet --project "$_PY_ROOT" python "$@"
    else
        python3 "$@"
    fi
}
