## index

+----------------+-----------------------------------+
| alias          | symbol                            |
+================+===================================+
| @code          | ../../jam/cast/code.cast          |
| @cmake         | cmake.cast                        |
| @project-info  | ../project-info.md                |
| @ProjectInfo   | ../Source/generated/ProjectInfo.h |
| @CMakeLists    | ../CMakeLists.txt                 |
| @build-windows | ../build-windows.sh               |
| @semicolon     | ;                                 |
+----------------+-----------------------------------+

## headers

+----------------+----------------------------------------------------------------------+---------+
| file           | brief                                                                | comment |
+================+======================================================================+=========+
| ProjectInfo.h  | ```                                                                  |         |
|                | @file ProjectInfo.h                                                  |         |
|                | @brief Project metadata — the ProjectInfo namespace, generated.      |         |
|                | ```                                                                  |         |
+----------------+----------------------------------------------------------------------+---------+
| CMakeLists.txt | ```                                                                  |         |
|                | @file CMakeLists.txt                                                 |         |
|                | @brief whatdbg build manifest — a self-sufficient console app.       |         |
|                |                                                                      |         |
|                | Generated from project-info.md; every value traces to one table row. |         |
|                | Edit the table, run cast, then configure.                            |         |
|                | ```                                                                  |         |
+----------------+----------------------------------------------------------------------+---------+

## output

+--------------------------------------------+--------------------+-----------------------------------------------+----------------+
| list                                       | separator          | structure                                     | file           |
+============================================+====================+===============================================+================+
| - list: @project-info:cmake                | - list: @semicolon | @cmake:cmake                                  | @CMakeLists    |
|                                            |                    | - comment: @headers:brief                     |                |
| - list: @project-info:project info         |                    |                                               |                |
| - list: @project-info:signing              |                    |                                               |                |
| - list: @project-info:architecture         | - list: @semicolon | - list: @cmake:value                          |                |
| - list: @project-info:release:stage=       | - list: @semicolon | - list: @cmake:mac                            |                |
| - list: @project-info:release:stage=linker | - list: @semicolon | - list: @cmake:mac                            |                |
| - list: @project-info:debug:stage=         | - list: @semicolon | - list: @cmake:mac                            |                |
| - list: @project-info:release:stage=       | - list: @semicolon | - list: @cmake:win                            |                |
| - list: @project-info:release:stage=linker | - list: @semicolon | - list: @cmake:win                            |                |
| - list: @project-info:debug:stage=         | - list: @semicolon | - list: @cmake:win                            |                |
| - list: @project-info:binary data          |                    | - list: @cmake:win                            |                |
| - list: @project-info:binary data          |                    | - list: @cmake:mac                            |                |
| - list: @project-info:user module          |                    | - list: @cmake:module                         |                |
| - list: @project-info:source glob          |                    | - list: @cmake:glob-pattern                   |                |
| - list: @project-info:define               |                    | - list: @cmake:value                          |                |
| - list: @project-info:include              |                    | - list: @cmake:value                          |                |
| - list: @project-info:juce module          |                    | - list: @cmake:value                          |                |
| - list: @project-info:user module          |                    | - list: @cmake:link                           |                |
|                                            |                    | - strip: @cmake:strip                         |                |
|                                            |                    | - codesign: @cmake:codesign                   |                |
|                                            |                    | - notarize: @cmake:notarize                   |                |
|                                            |                    | - install-directory: @cmake:install-directory |                |
|                                            |                    | - install-copy: @cmake:install-copy           |                |
|                                            |                    | - install-rename: @cmake:install-rename       |                |
|                                            |                    | - postinstall: @cmake:postinstall             |                |
|                                            |                    | - xattr: @cmake:xattr                         |                |
|                                            |                    | - verify: @cmake:verify                       |                |
|                                            |                    | - pkg-staging: @cmake:pkg-staging             |                |
|                                            |                    | - pkgbuild: @cmake:pkgbuild                   |                |
|                                            |                    | - productsign: @cmake:productsign             |                |
|                                            |                    | - staple: @cmake:staple                       |                |
|                                            |                    | - qa-directory: @cmake:qa-directory           |                |
|                                            |                    | - qa-copy: @cmake:qa-copy                     |                |
+--------------------------------------------+--------------------+-----------------------------------------------+----------------+
| > - list: @project-info:project info       |                    | @code:namespace                               | @ProjectInfo   |
|                                            |                    | - macro: #pragma once                         |                |
|                                            |                    | - name: ProjectInfo                           |                |
|                                            |                    | - comment: @headers:brief                     |                |
|                                            |                    | > - list: @code:constant                      |                |
+--------------------------------------------+--------------------+-----------------------------------------------+----------------+
|                                            |                    | @cmake:windows-build                          | @build-windows |
+--------------------------------------------+--------------------+-----------------------------------------------+----------------+
