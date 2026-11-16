#!/bin/sh
# Build SY1's site: a release of VERSION, the browser emulator, fm1-base's pages (front page, installer, manual)
# and the Patch Manager, in app/build/site. With --push it updates origin/gh-pages without rewriting history.
#   tools/publish_site.sh VERSION [--push]
set -e
cd "$(dirname "$0")/.."
V="${1:?usage: tools/publish_site.sh VERSION [--push]}"
PY="${PYTHON:-python3}"
./build.sh --release "$V"
./build.sh --web --release "$V"
"$PY" tools/make_site.py "app/build/sy1-$V.fwsc" "$V" app/build/site
touch app/build/site/.nojekyll
echo "site: app/build/site"
[ "${2:-}" = "--push" ] || exit 0
REMOTE="$(git remote get-url origin)"
SHA="$(git rev-parse --short HEAD)"
AUTHOR_NAME="$(git log -1 --format=%an)"
AUTHOR_EMAIL="$(git log -1 --format=%ae)"
DEPLOY="$(mktemp -d "$PWD/app/build/pages.XXXXXX")"
trap 'rm -rf "$DEPLOY"' EXIT
git clone -q --depth 1 --branch gh-pages "$REMOTE" "$DEPLOY"
rsync -a --delete --exclude=.git app/build/site/ "$DEPLOY/"
git -C "$DEPLOY" add -A
if ! git -C "$DEPLOY" diff --cached --quiet; then
    git -C "$DEPLOY" -c user.name="$AUTHOR_NAME" -c user.email="$AUTHOR_EMAIL" \
        commit -q -m "Site for SY1 $V (from $SHA)"
    git -C "$DEPLOY" push -q origin gh-pages
fi
echo "pushed the site for SY1 $V to gh-pages"
