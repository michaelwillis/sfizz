#!/bin/bash

set -e

doxygen Doxyfile
git fetch --depth=1 https://github.com/${TRAVIS_REPO_SLUG}.git refs/heads/gh-pages:refs/remotes/origin/gh-pages
git checkout origin/gh-pages
git checkout -b gh-pages
rm -rf ./api
mv _api api
git add api && git commit -m "Travis build: ${TRAVIS_BUILD_NUMBER}"
git remote add origin-pages https://${GITHUB_TOKEN}@github.com/${TRAVIS_REPO_SLUG}.git > /dev/null 2>&1
git push --quiet --set-upstream origin-pages gh-pages