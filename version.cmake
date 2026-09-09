# Commit identity for builds made from a source archive.
#
# .gitattributes marks this file `export-subst`, so `git archive` -- and every
# archiver that asks Git for file content, including the "Source code" tarballs
# GitHub generates for a tag -- replaces the two placeholders below with the
# archived commit's hash and its ref names. cmake/GitVersion.cmake reads them
# when no Git working tree is available, so that a binary built from such an
# archive reports the commit it was built from rather than an unknown tag.
#
# In a Git working tree the placeholders are left exactly as written and are
# ignored: there the commit is read from Git itself. An archiver that copies the
# working tree instead of asking Git for its content -- git-archive-all, used by
# the source-archive CI job, is one -- performs no substitution either, and an
# archive it produced still reports an unknown tag.
#
# The file is valid CMake so that it can be included, but GitVersion.cmake parses
# it textually: an archive is untrusted input, and an unsubstituted placeholder
# must never be mistaken for a commit hash.
#
# docs/COMPILING_DEBUGGING_TESTING.md, section "Build-system changes beyond the
# dialect switch", describes the whole flow and why it was added.
set(MONERO_ARCHIVE_COMMIT_HASH "$Format:%H$")
set(MONERO_ARCHIVE_COMMIT_REFS "$Format:%D$")
