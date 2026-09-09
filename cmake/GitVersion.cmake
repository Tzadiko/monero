# Copyright (c) 2014-2024, The Monero Project
# 
# All rights reserved.
# 
# Redistribution and use in source and binary forms, with or without modification, are
# permitted provided that the following conditions are met:
# 
# 1. Redistributions of source code must retain the above copyright notice, this list of
#    conditions and the following disclaimer.
# 
# 2. Redistributions in binary form must reproduce the above copyright notice, this list
#    of conditions and the following disclaimer in the documentation and/or other
#    materials provided with the distribution.
# 
# 3. Neither the name of the copyright holder nor the names of its contributors may be
#    used to endorse or promote products derived from this software without specific
#    prior written permission.
# 
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY
# EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
# MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
# THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
# PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
# INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
# STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
# THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
# 
# Parts of this file are originally copyright (c) 2012-2013 The Cryptonote developers

# Check what commit we're on

# Read the commit identity that `git archive` stamps into the top-level
# version.cmake, which .gitattributes marks `export-subst` for that purpose.
# Why this fallback exists, and why the stamp is treated as untrusted input, is
# recorded in docs/COMPILING_DEBUGGING_TESTING.md, section "Build-system changes
# beyond the dialect switch".
#
# Sets VERSIONTAG and VERSION_IS_RELEASE in the caller's scope, and leaves
# VERSIONTAG empty when the tree carries no usable stamp. That is the normal case
# in a Git working tree, where the placeholders are verbatim and the commit comes
# from Git itself, and also in an archive produced by a tool that copies the
# working tree rather than asking Git for its content.
#
# The stamp is parsed textually instead of being included: an archive is
# untrusted input, and a verbatim placeholder must never be mistaken for a hash.
function (get_version_tag_from_archive)
    set(VERSIONTAG "" PARENT_SCOPE)
    set(VERSION_IS_RELEASE "false" PARENT_SCOPE)

    # Anchored on the directory holding this file rather than on whichever
    # listfile is being processed when the function runs, so the stamp is found
    # wherever the project is included from.
    set(ARCHIVE_STAMP "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../version.cmake")
    if(NOT EXISTS "${ARCHIVE_STAMP}")
        return()
    endif()

    file(READ "${ARCHIVE_STAMP}" ARCHIVE_STAMP_CONTENT)
    if(NOT ARCHIVE_STAMP_CONTENT MATCHES "MONERO_ARCHIVE_COMMIT_HASH[ \t]+\"([0-9a-f]+)\"")
        return()
    endif()

    # Accept nothing but a complete, substituted hash: git writes the full 40
    # hexadecimal characters, and an unsubstituted placeholder cannot match them.
    set(COMMIT "${CMAKE_MATCH_1}")
    string(LENGTH "${COMMIT}" COMMIT_LENGTH)
    if(NOT COMMIT_LENGTH EQUAL 40)
        return()
    endif()

    string(SUBSTRING "${COMMIT}" 0 9 COMMIT)
    message(STATUS "You are building from a source archive of commit ${COMMIT}")

    # The ref names stamped beside the hash carry a tag when the archive was cut
    # from a tagged commit, which is the condition the Git working tree path
    # below reports as a release build.
    set(ARCHIVE_IS_TAGGED "false")
    if(ARCHIVE_STAMP_CONTENT MATCHES "MONERO_ARCHIVE_COMMIT_REFS[ \t]+\"([^\"]*)\"")
        if("${CMAKE_MATCH_1}" MATCHES "tag: ")
            set(ARCHIVE_IS_TAGGED "true")
        endif()
    endif()

    if(ARCHIVE_IS_TAGGED STREQUAL "true")
        message(STATUS "You are building a tagged release")
        set(VERSIONTAG "release" PARENT_SCOPE)
        set(VERSION_IS_RELEASE "true" PARENT_SCOPE)
    else()
        set(VERSIONTAG "${COMMIT}" PARENT_SCOPE)
        set(VERSION_IS_RELEASE "false" PARENT_SCOPE)
    endif()
endfunction()

function (get_version_tag_from_git GIT)
    execute_process(COMMAND "${GIT}" rev-parse --short=9 HEAD
                    WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}
                    RESULT_VARIABLE RET
                    OUTPUT_VARIABLE COMMIT
                    ERROR_VARIABLE GIT_ERROR
                    OUTPUT_STRIP_TRAILING_WHITESPACE
                    ERROR_STRIP_TRAILING_WHITESPACE)

    if(RET)
        # There is no Git information here. Building from a source archive is the
        # ordinary reason, and an archive created by `git archive` carries the
        # commit identity in version.cmake instead, so ask for that before
        # giving up. Git's own diagnostic was captured above rather than printed,
        # and is reported below only if nothing identifies the commit.

        get_version_tag_from_archive()

        if(VERSIONTAG STREQUAL "")
            # Something went wrong, set the version tag to -unknown

            message(WARNING "Cannot determine current commit. Make sure that you are building either from a Git working tree or from a source archive.\n"
                            "Git reported: ${GIT_ERROR}")
            set(VERSIONTAG "unknown")
            set(VERSION_IS_RELEASE "false")
        endif()
    else()
        string(SUBSTRING ${COMMIT} 0 9 COMMIT)
        message(STATUS "You are currently on commit ${COMMIT}")

        # Get all the tags
        execute_process(COMMAND "${GIT}" tag -l --points-at HEAD
                        WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}
                        RESULT_VARIABLE RET
                        OUTPUT_VARIABLE TAG
                        OUTPUT_STRIP_TRAILING_WHITESPACE)

        # Check if we're building that tagged commit or a different one
        if(TAG)
            message(STATUS "You are building a tagged release")
            set(VERSIONTAG "release")
            set(VERSION_IS_RELEASE "true")
        else()
            message(STATUS "You are ahead of or behind a tagged release")
            set(VERSIONTAG "${COMMIT}")
            set(VERSION_IS_RELEASE "false")
        endif()	    
    endif()

    set(VERSIONTAG "${VERSIONTAG}" PARENT_SCOPE)
    set(VERSION_IS_RELEASE "${VERSION_IS_RELEASE}" PARENT_SCOPE)
endfunction()
