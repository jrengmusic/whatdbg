## index

+------------------+-----------------------------------+
| alias            | symbol                            |
+==================+===================================+
| @code            | ../../jam/cast/code.cast          |
| @cmake           | cmake.cast                        |
| @project-info    | ../project-info.md                |
| @modules         | ../../jam/user-modules-info.md    |
| @juce            | ../../jam/cast/juce.cast          |
| @ProjectInfo     | ../Source/generated/ProjectInfo.h |
| @CMakeLists      | ../CMakeLists.txt                 |
| @signing         | signing.md                        |
| @Entitlements    | ../entitlements.plist             |
| @installer       | installer.cast                    |
| @Distribution    | installer/mac/distribution.xml    |
| @Installer       | installer/win/installer.nsi       |
| @Gh              | ../gh.cmake                       |
| @Release         | ../RELEASE.md                     |
| @semicolon       | ;                                 |
| @source          | source.cast                       |
| @identifiers     | identifiers.md                    |
| @Identifiers     | ../Source/generated/Identifiers.h |
| @GeneratedSource | ../Source/generated/Generated.cpp |
| @Generated       | ../Source/generated/Generated.h   |
+------------------+-----------------------------------+

## headers

+---------------+-------------------------------------------------------------------------------+-------------------------------------------+
| file          | brief                                                                         | description                               |
+===============+===============================================================================+===========================================+
| ProjectInfo.h | ```                                                                           | Product name, version, and source commit. |
|               | @file ProjectInfo.h                                                           |                                           |
|               | @brief Project metadata — the ProjectInfo namespace, generated.               |                                           |
|               | ```                                                                           |                                           |
+---------------+-------------------------------------------------------------------------------+-------------------------------------------+
| Identifiers.h | ```                                                                           | Every Id:: name.                          |
|               | @file Identifiers.h                                                           |                                           |
|               | @brief DAP vocabulary.                                                        |                                           |
|               | ```                                                                           |                                           |
+---------------+-------------------------------------------------------------------------------+-------------------------------------------+
| Generated.h   | ```                                                                           | Include umbrella for every generated      |
|               | @file Generated.h                                                             | header.                                   |
|               | @brief whatdbg's generated-header umbrella — includes every generated header. |                                           |
|               | ```                                                                           |                                           |
+---------------+-------------------------------------------------------------------------------+-------------------------------------------+

## output

+-----------------------------------------------+----------------------+-----------------------------------------------+------------------+
| list                                          | separator            | structure                                     | file             |
+===============================================+======================+===============================================+==================+
| - [list]: @project-info:cmake                 | - [list]: @semicolon | @cmake:cmake                                  | @CMakeLists      |
| - [list]: @project-info:project info          |                      |                                               |                  |
| - [list]: @signing:signing                    |                      |                                               |                  |
| - [list]: @project-info:release:stage=        | - [list]: @semicolon | - [list]: @cmake:mac                          |                  |
| - [list]: @project-info:release:stage=linker  | - [list]: @semicolon | - [list]: @cmake:mac                          |                  |
| - [list]: @project-info:debug:stage=          | - [list]: @semicolon | - [list]: @cmake:mac                          |                  |
| - [list]: @project-info:release:stage=        | - [list]: @semicolon | - [list]: @cmake:win                          |                  |
| - [list]: @project-info:release:stage=linker  | - [list]: @semicolon | - [list]: @cmake:win                          |                  |
| - [list]: @project-info:debug:stage=          | - [list]: @semicolon | - [list]: @cmake:win                          |                  |
| - [list]: @project-info:release:stage=        | - [list]: @semicolon | - [list]: @cmake:mac                          |                  |
| - [list]: @project-info:release:stage=linker  | - [list]: @semicolon | - [list]: @cmake:win                          |                  |
| - [list]: @project-info:debug:stage=          | - [list]: @semicolon | - [list]: @cmake:mac                          |                  |
| - [list]: @project-info:user module           |                      | - [list]: @cmake:module                       |                  |
| - [list]: @project-info:source glob           |                      | - [list]: @cmake:glob-pattern                 |                  |
| - [list]: @project-info:macro                 |                      | - [list]: @cmake:value                        |                  |
| - [list]: @project-info:include               |                      | - [list]: @cmake:value                        |                  |
| - [list]: @project-info:juce module           |                      | - [list]: @cmake:value                        |                  |
| - [list]: @project-info:user module           |                      | - [list]: @cmake:link                         |                  |
| - [list]: @project-info:library               |                      | - [list]: @cmake:value                        |                  |
| - [list]: @project-info:binary                |                      | - [list]: @cmake:value                        |                  |
| - [list]: @project-info:pack                  |                      | - [list]: @cmake:pack-value                   |                  |
|                                               |                      | - strip: @cmake:strip                         |                  |
|                                               |                      | - codesign: @cmake:codesign                   |                  |
|                                               |                      | - notarize: @cmake:notarize                   |                  |
|                                               |                      | - install-directory: @cmake:install-directory |                  |
|                                               |                      | - install-copy: @cmake:install-copy           |                  |
|                                               |                      | - install-rename: @cmake:install-rename       |                  |
|                                               |                      | - postinstall: @cmake:postinstall             |                  |
|                                               |                      | - xattr: @cmake:xattr                         |                  |
|                                               |                      | - verify: @cmake:verify                       |                  |
|                                               |                      | - pkg-staging: @cmake:pkg-staging             |                  |
|                                               |                      | - pkgbuild: @cmake:pkgbuild                   |                  |
|                                               |                      | - productsign: @cmake:productsign             |                  |
|                                               |                      | - staple: @cmake:staple                       |                  |
|                                               |                      | - qa-directory: @cmake:qa-directory           |                  |
|                                               |                      | - qa-copy: @cmake:qa-copy                     |                  |
|                                               |                      | - productbuild: @cmake:productbuild           |                  |
|                                               |                      | - makensis: @cmake:makensis                   |                  |
|                                               |                      | - patched-juce: @juce:patched-juce            |                  |
| - [list]: @modules:submodule:module=patch     |                      | - [list]: @juce:patch                         |                  |
+-----------------------------------------------+----------------------+-----------------------------------------------+------------------+
| - [list]: @project-info:cmake                 |                      | @installer:distribution                       | @Distribution    |
| - [list]: @project-info:project info          |                      |                                               |                  |
+-----------------------------------------------+----------------------+-----------------------------------------------+------------------+
| - [list]: @project-info:project info          |                      | @installer:installer                          | @Installer       |
| - [list]: @project-info:cmake                 |                      |                                               |                  |
| - [list]: @signing:signing                    |                      |                                               |                  |
+-----------------------------------------------+----------------------+-----------------------------------------------+------------------+
| - [list]: @project-info:cmake                 |                      | @cmake:gh                                     | @Gh              |
| - [list]: @project-info:project info          |                      | - [list]: @cmake:pack-value                   |                  |
| - [list]: @project-info:pack                  |                      |                                               |                  |
+-----------------------------------------------+----------------------+-----------------------------------------------+------------------+
| - [list]: @project-info:cmake                 |                      | @installer:[no-banner]release                 | @Release         |
| - [list]: @project-info:release notes         |                      | - [list]: @installer:note                     |                  |
+-----------------------------------------------+----------------------+-----------------------------------------------+------------------+
| > - [list]: @signing:signing:type=entitlement |                      | @code:[xml]entitlements                       | @Entitlements    |
|                                               |                      | > - [list]: @code:entitlement                 |                  |
+-----------------------------------------------+----------------------+-----------------------------------------------+------------------+
| > - [list]: @project-info:project info        |                      | @code:namespace                               | @ProjectInfo     |
|                                               |                      | - macro: #pragma once                         |                  |
|                                               |                      | - name: ProjectInfo                           |                  |
|                                               |                      | - [description]: @headers:brief               |                  |
|                                               |                      | > - [list]: @code:constant                    |                  |
+-----------------------------------------------+----------------------+-----------------------------------------------+------------------+
| - [list]: @identifiers:identifiers            |                      | @code:namespace                               | @Identifiers     |
|                                               |                      | - macro: #pragma once                         |                  |
|                                               |                      | - generated:                                  |                  |
|                                               |                      | - name: Id                                    |                  |
|                                               |                      | - [description]: @headers:brief               |                  |
|                                               |                      | - [list]: @code:identifier                    |                  |
+-----------------------------------------------+----------------------+-----------------------------------------------+------------------+
| > - [list]: @identifiers:identifiers          |                      | @source:source                                | @GeneratedSource |
|                                               |                      | - file: Generated.h                           |                  |
|                                               |                      |                                               |                  |
|                                               |                      | @code:namespace                               |                  |
|                                               |                      | - name: Id                                    |                  |
|                                               |                      | > - [list]: @code:identifier-definition       |                  |
+-----------------------------------------------+----------------------+-----------------------------------------------+------------------+

## output index

+---------------------------------------+-----------+-------------------------+------------+
| list                                  | separator | structure               | file       |
+=======================================+===========+=========================+============+
| - [list]: @headers:file=ProjectInfo.h |           | @code:line              | @Generated |
|                                       |           | - macro: #pragma once   |            |
|                                       |           |                         |            |
|                                       |           | - [list]: @code:include |            |
+---------------------------------------+-----------+-------------------------+------------+
| - [list]: @headers:file=Identifiers.h |           | @code:line              | @Generated |
|                                       |           | - macro: #pragma once   |            |
|                                       |           |                         |            |
|                                       |           | - [list]: @code:include |            |
+---------------------------------------+-----------+-------------------------+------------+
