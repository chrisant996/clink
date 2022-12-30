// Generated from t3070-wildmatch.sh by 'premake5 embed'.

// Test case format:
//
//  <x>match <wmode> <fnmode> <string> <pattern>
//  <x>imatch <wmode> <string> <pattern>
//  <x>pathmatch <wmode> <string> <pattern>
//
// match        Tests with wildmatch() and fnmatch(), and with slashes and backslashes.
// imatch       Tests with wildmatch() ignoring case, with slashes and backslashes.
// pathmatch    Tests with wildmatch() without WM_PATHNAME, with slashes and backslashes.
//
// <x>          / to run the test only with the verbatim <string>,
//              \ to run the test only with slashes in <string> converted to backslashes,
//              or leave off <x> to run the test once each way.
//
// <wmode>      1 if the test is expected to match with wildmatch(),
//              0 if the test is expected to fail,
//              or any other value to skip running the test with wildmatch().
//
// <fnmode>     1 if the test is expected to match with fnmatch(),
//              0 if the test is expected to fail,
//              or any other value to skip running the test with fnmatch().

static const char* const c_cases[] = {

// Basic wildmat features
"match 1 1 foo foo",                                    // case #1
"match 0 0 foo bar",                                    // case #2
"match 1 1 '' \"\"",                                    // case #3
"match 1 1 foo '???'",                                  // case #4
"match 0 0 foo '??'",                                   // case #5
"match 1 1 foo '*'",                                    // case #6
"match 1 1 foo 'f*'",                                   // case #7
"match 0 0 foo '*f'",                                   // case #8
"match 1 1 foo '*foo*'",                                // case #9
"match 1 1 foobar '*ob*a*r*'",                          // case #10
"match 1 1 aaaaaaabababab '*ab'",                       // case #11
"match 1 1 'foo*' 'foo\\*'",                            // case #12
"match 0 0 foobar 'foo\\*bar'",                         // case #13
"match 1 1 'f\\oo' 'f\\\\oo'",                          // case #14
"match 1 1 ball '*[al]?'",                              // case #15
"match 0 0 ten '[ten]'",                                // case #16
"match 0 1 ten '**[!te]'",                              // case #17
"match 0 0 ten '**[!ten]'",                             // case #18
"match 1 1 ten 't[a-g]n'",                              // case #19
"match 0 0 ten 't[!a-g]n'",                             // case #20
"match 1 1 ton 't[!a-g]n'",                             // case #21
"match 1 1 ton 't[^a-g]n'",                             // case #22
"match 1 x 'a]b' 'a[]]b'",                              // case #23
"match 1 x a-b 'a[]-]b'",                               // case #24
"match 1 x 'a]b' 'a[]-]b'",                             // case #25
"match 0 x aab 'a[]-]b'",                               // case #26
"match 1 x aab 'a[]a-]b'",                              // case #27
"match 1 1 ']' ']'",                                    // case #28

// Extended slash-matching features
"match 0 0 'foo/baz/bar' 'foo*bar'",                    // case #29
"match 0 0 'foo/baz/bar' 'foo**bar'",                   // case #30
"match 0 1 'foobazbar' 'foo**bar'",                     // case #31
"match 1 1 'foo/baz/bar' 'foo/**/bar'",                 // case #32
"match 1 0 'foo/baz/bar' 'foo/**/**/bar'",              // case #33
"match 1 0 'foo/b/a/z/bar' 'foo/**/bar'",               // case #34
"match 1 0 'foo/b/a/z/bar' 'foo/**/**/bar'",            // case #35
"match 1 0 'foo/bar' 'foo/**/bar'",                     // case #36
"match 1 0 'foo/bar' 'foo/**/**/bar'",                  // case #37
"match 0 0 'foo/bar' 'foo?bar'",                        // case #38
"match 0 0 'foo/bar' 'foo[/]bar'",                      // case #39
"match 0 0 'foo/bar' 'foo[^a-z]bar'",                   // case #40  **ADDITIONAL**
"match 0 0 'foo/bar' 'f[^eiu][^eiu][^eiu][^eiu][^eiu]r'",  // case #41
"match 1 1 'foo-bar' 'f[^eiu][^eiu][^eiu][^eiu][^eiu]r'",  // case #42
"match 1 0 'foo' '**/foo'",                             // case #43
"match 1 x 'XXX/foo' '**/foo'",                         // case #44
"match 1 0 'bar/baz/foo' '**/foo'",                     // case #45
"match 0 0 'bar/baz/foo' '*/foo'",                      // case #46
"match 0 0 'foo/bar/baz' '**/bar*'",                    // case #47
"match 1 0 'deep/foo/bar/baz' '**/bar/*'",              // case #48
"match 0 0 'deep/foo/bar/baz/' '**/bar/*'",             // case #49
"match 1 0 'deep/foo/bar/baz/' '**/bar/**'",            // case #50
"match 0 0 'deep/foo/bar' '**/bar/*'",                  // case #51
"match 1 0 'deep/foo/bar/' '**/bar/**'",                // case #52
"match 0 0 'foo/bar/baz' '**/bar**'",                   // case #53
"match 1 0 'foo/bar/baz/x' '*/bar/**'",                 // case #54
"match 0 0 'deep/foo/bar/baz/x' '*/bar/**'",            // case #55
"match 1 0 'deep/foo/bar/baz/x' '**/bar/*/*'",          // case #56

// Various additional tests
"match 0 0 'acrt' 'a[c-c]st'",                          // case #57
"match 1 1 'acrt' 'a[c-c]rt'",                          // case #58
"match 0 0 ']' '[!]-]'",                                // case #59
"match 1 x 'a' '[!]-]'",                                // case #60
"match 0 0 '' '\\'",                                    // case #61
"match 0 x '\\' '\\'",                                  // case #62
"match 0 x 'XXX/\\' '*/\\'",                            // case #63
"match 1 x 'XXX/\\' '*/\\\\'",                          // case #64
"match 1 1 'foo' 'foo'",                                // case #65
"match 1 1 '@foo' '@foo'",                              // case #66
"match 0 0 'foo' '@foo'",                               // case #67
"match 1 1 '[ab]' '\\[ab]'",                            // case #68
"match 1 1 '[ab]' '[[]ab]'",                            // case #69
"match 1 x '[ab]' '[[:]ab]'",                           // case #70
"match 0 x '[ab]' '[[::]ab]'",                          // case #71
"match 1 x '[ab]' '[[:digit]ab]'",                      // case #72
"match 1 x '[ab]' '[\\[:]ab]'",                         // case #73
"match 1 1 '?a?b' '\\??\\?b'",                          // case #74
"match 1 1 'abc' '\\a\\b\\c'",                          // case #75
"match 0 0 'foo' ''",                                   // case #76
"match 1 0 'foo/bar/baz/to' '**/t[o]'",                 // case #77

// Character class tests
"match 1 x 'a1B' '[[:alpha:]][[:digit:]][[:upper:]]'",  // case #78
"match 0 x 'a' '[[:digit:][:upper:][:space:]]'",        // case #79
"match 1 x 'A' '[[:digit:][:upper:][:space:]]'",        // case #80
"match 1 x '1' '[[:digit:][:upper:][:space:]]'",        // case #81
"match 0 x '1' '[[:digit:][:upper:][:spaci:]]'",        // case #82
"match 1 x ' ' '[[:digit:][:upper:][:space:]]'",        // case #83
"match 0 x '.' '[[:digit:][:upper:][:space:]]'",        // case #84
"match 1 x '.' '[[:digit:][:punct:][:space:]]'",        // case #85
"match 1 x '5' '[[:xdigit:]]'",                         // case #86
"match 1 x 'f' '[[:xdigit:]]'",                         // case #87
"match 1 x 'D' '[[:xdigit:]]'",                         // case #88
"match 1 x '_' '[[:alnum:][:alpha:][:blank:][:cntrl:][:digit:][:graph:][:lower:][:print:][:punct:][:space:][:upper:][:xdigit:]]'",  // case #89
"match 1 x '_' '[[:alnum:][:alpha:][:blank:][:cntrl:][:digit:][:graph:][:lower:][:print:][:punct:][:space:][:upper:][:xdigit:]]'",  // case #90
"match 1 x '.' '[^[:alnum:][:alpha:][:blank:][:cntrl:][:digit:][:lower:][:space:][:upper:][:xdigit:]]'",  // case #91
"match 1 x '5' '[a-c[:digit:]x-z]'",                    // case #92
"match 1 x 'b' '[a-c[:digit:]x-z]'",                    // case #93
"match 1 x 'y' '[a-c[:digit:]x-z]'",                    // case #94
"match 0 x 'q' '[a-c[:digit:]x-z]'",                    // case #95

// Additional tests, including some malformed wildmats
"match 1 x ']' '[\\\\-^]'",                             // case #96
"match 0 0 '[' '[\\\\-^]'",                             // case #97
"match 1 x '-' '[\\-_]'",                               // case #98
"match 1 x ']' '[\\]]'",                                // case #99
"match 0 0 '\\]' '[\\]]'",                              // case #100
"match 0 0 '\\' '[\\]]'",                               // case #101
"match 0 0 'ab' 'a[]b'",                                // case #102
"match 0 x 'a[]b' 'a[]b'",                              // case #103
"match 0 x 'ab[' 'ab['",                                // case #104
"match 0 0 'ab' '[!'",                                  // case #105
"match 0 0 'ab' '[-'",                                  // case #106
"match 1 1 '-' '[-]'",                                  // case #107
"match 0 0 '-' '[a-'",                                  // case #108
"match 0 0 '-' '[!a-'",                                 // case #109
"match 1 x '-' '[--A]'",                                // case #110
"match 1 x '5' '[--A]'",                                // case #111
"match 1 1 ' ' '[ --]'",                                // case #112
"match 1 1 '$' '[ --]'",                                // case #113
"match 1 1 '-' '[ --]'",                                // case #114
"match 0 0 '0' '[ --]'",                                // case #115
"match 1 x '-' '[---]'",                                // case #116
"match 1 x '-' '[------]'",                             // case #117
"match 0 0 'j' '[a-e-n]'",                              // case #118
"match 1 x '-' '[a-e-n]'",                              // case #119
"match 1 x 'a' '[!------]'",                            // case #120
"match 0 0 '[' '[]-a]'",                                // case #121
"match 1 x '^' '[]-a]'",                                // case #122
"match 0 0 '^' '[!]-a]'",                               // case #123
"match 1 x '[' '[!]-a]'",                               // case #124
"match 1 1 '^' '[a^bc]'",                               // case #125
"match 1 x '-b]' '[a-]b]'",                             // case #126
"match 0 0 '\\' '[\\]'",                                // case #127
"/match 1 1 '\\' '[\\\\]'",                             // case #128  **MODIFIED**
"match 0 0 '\\' '[!\\\\]'",                             // case #129
"match 1 1 'G' '[A-\\\\]'",                             // case #130
"match 0 0 'aaabbb' 'b*a'",                             // case #131
"match 0 0 'aabcaa' '*ba*'",                            // case #132
"match 1 1 ',' '[,]'",                                  // case #133
"match 1 1 ',' '[\\\\,]'",                              // case #134
"/match 1 1 '\\' '[\\\\,]'",                            // case #135  **MODIFIED**
"match 1 1 '-' '[,-.]'",                                // case #136
"match 0 0 '+' '[,-.]'",                                // case #137
"match 0 0 '-.]' '[,-.]'",                              // case #138
"match 1 1 '2' '[\\1-\\3]'",                            // case #139
"match 1 1 '3' '[\\1-\\3]'",                            // case #140
"match 0 0 '4' '[\\1-\\3]'",                            // case #141
"/match 1 1 '\\' '[[-\\]]'",                            // case #142  **MODIFIED**
"match 1 1 '[' '[[-\\]]'",                              // case #143
"match 1 1 ']' '[[-\\]]'",                              // case #144
"match 0 0 '-' '[[-\\]]'",                              // case #145

// Test recursion and the abort code (use "wildtest -i" to see iteration counts)
"match 1 1 '-adobe-courier-bold-o-normal--12-120-75-75-m-70-iso8859-1' '-*-*-*-*-*-*-12-*-*-*-m-*-*-*'",  // case #146
"match 0 0 '-adobe-courier-bold-o-normal--12-120-75-75-X-70-iso8859-1' '-*-*-*-*-*-*-12-*-*-*-m-*-*-*'",  // case #147
"match 0 0 '-adobe-courier-bold-o-normal--12-120-75-75-/-70-iso8859-1' '-*-*-*-*-*-*-12-*-*-*-m-*-*-*'",  // case #148
"match 1 1 'XXX/adobe/courier/bold/o/normal//12/120/75/75/m/70/iso8859/1' 'XXX/*/*/*/*/*/*/12/*/*/*/m/*/*/*'",  // case #149
"match 0 0 'XXX/adobe/courier/bold/o/normal//12/120/75/75/X/70/iso8859/1' 'XXX/*/*/*/*/*/*/12/*/*/*/m/*/*/*'",  // case #150
"match 1 0 'abcd/abcdefg/abcdefghijk/abcdefghijklmnop.txt' '**/*a*b*g*n*t'",  // case #151
"match 0 0 'abcd/abcdefg/abcdefghijk/abcdefghijklmnop.txtz' '**/*a*b*g*n*t'",  // case #152
"match 0 x foo '*/*/*'",                                // case #153
"match 0 x foo/bar '*/*/*'",                            // case #154
"match 1 x foo/bba/arr '*/*/*'",                        // case #155
"match 0 x foo/bb/aa/rr '*/*/*'",                       // case #156
"match 1 x foo/bb/aa/rr '**/**/**'",                    // case #157
"match 1 x abcXdefXghi '*X*i'",                         // case #158
"match 0 x ab/cXd/efXg/hi '*X*i'",                      // case #159
"match 1 x ab/cXd/efXg/hi '*/*X*/*/*i'",                // case #160
"match 1 x ab/cXd/efXg/hi '**/*X*/**/*i'",              // case #161

"pathmatch 1 foo foo",                                  // case #162
"pathmatch 0 foo fo",                                   // case #163
"pathmatch 1 foo/bar foo/bar",                          // case #164
"pathmatch 1 foo/bar 'foo/*'",                          // case #165
"pathmatch 1 foo/bba/arr 'foo/*'",                      // case #166
"pathmatch 1 foo/bba/arr 'foo/**'",                     // case #167
"pathmatch 1 foo/bba/arr 'foo*'",                       // case #168
"pathmatch 1 foo/bba/arr 'foo**'",                      // case #169
"pathmatch 1 foo/bba/arr 'foo/*arr'",                   // case #170
"pathmatch 1 foo/bba/arr 'foo/**arr'",                  // case #171
"pathmatch 0 foo/bba/arr 'foo/*z'",                     // case #172
"pathmatch 0 foo/bba/arr 'foo/**z'",                    // case #173
"pathmatch 1 foo/bar 'foo?bar'",                        // case #174
"pathmatch 1 foo/bar 'foo[/]bar'",                      // case #175
"pathmatch 1 foo/bar 'foo[^a-z]bar'",                   // case #176  **ADDITIONAL**
"pathmatch 0 foo '*/*/*'",                              // case #177
"pathmatch 0 foo/bar '*/*/*'",                          // case #178
"pathmatch 1 foo/bba/arr '*/*/*'",                      // case #179
"pathmatch 1 foo/bb/aa/rr '*/*/*'",                     // case #180
"pathmatch 1 abcXdefXghi '*X*i'",                       // case #181
"pathmatch 1 ab/cXd/efXg/hi '*/*X*/*/*i'",              // case #182
"pathmatch 1 ab/cXd/efXg/hi '*Xg*i'",                   // case #183

// Case-sensitivy features
"match 0 x 'a' '[A-Z]'",                                // case #184
"match 1 x 'A' '[A-Z]'",                                // case #185
"match 0 x 'A' '[a-z]'",                                // case #186
"match 1 x 'a' '[a-z]'",                                // case #187
"match 0 x 'a' '[[:upper:]]'",                          // case #188
"match 1 x 'A' '[[:upper:]]'",                          // case #189
"match 0 x 'A' '[[:lower:]]'",                          // case #190
"match 1 x 'a' '[[:lower:]]'",                          // case #191
"match 0 x 'A' '[B-Za]'",                               // case #192
"match 1 x 'a' '[B-Za]'",                               // case #193
"match 0 x 'A' '[B-a]'",                                // case #194
"match 1 x 'a' '[B-a]'",                                // case #195
"match 0 x 'z' '[Z-y]'",                                // case #196
"match 1 x 'Z' '[Z-y]'",                                // case #197

"imatch 1 'a' '[A-Z]'",                                 // case #198
"imatch 1 'A' '[A-Z]'",                                 // case #199
"imatch 1 'A' '[a-z]'",                                 // case #200
"imatch 1 'a' '[a-z]'",                                 // case #201
"imatch 1 'a' '[[:upper:]]'",                           // case #202
"imatch 1 'A' '[[:upper:]]'",                           // case #203
"imatch 1 'A' '[[:lower:]]'",                           // case #204
"imatch 1 'a' '[[:lower:]]'",                           // case #205
"imatch 1 'A' '[B-Za]'",                                // case #206
"imatch 1 'a' '[B-Za]'",                                // case #207
"imatch 1 'A' '[B-a]'",                                 // case #208
"imatch 1 'a' '[B-a]'",                                 // case #209
"imatch 1 'z' '[Z-y]'",                                 // case #210
"imatch 1 'Z' '[Z-y]'",                                 // case #211

// Additional edge cases
"match 1 0 'deep/foo/bar/baz/x' 'deep/**'",             // case #212
"match 1 0 'deep/foo/bar/baz/x' 'deep/***'",            // case #213
"match 1 0 'deep/foo/bar/baz/x' 'deep/*****'",          // case #214
"match 1 0 'deep/foo/bar/baz/x' 'deep/******'",         // case #215
"match 1 0 'deep/foo/bar/baz/x' 'deep/***/**'",         // case #216
"match 1 0 'deep/foo/bar/baz/x' 'deep/***/***'",        // case #217
"match 1 0 'deep/foo/bar/baz/x' 'deep/**/***/****'",    // case #218  **MODIFIED**
"match 1 1 'deep/foo/bar/baz/x' 'deep/**/***/****/*****'",  // case #219  **ADDITIONAL**

};

static const int c_expected_count = 219;
