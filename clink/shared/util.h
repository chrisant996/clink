/* Copyright (c) 2012 Martin Ridgers
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef UTIL_H
#define UTIL_H

void str_cpy(char* dest, const char* src, int max);
void str_cat(char* dest, const char* src, int max);
void str_cat_n(char* dest, const char* src, int max, int n);
void get_config_dir(char* buffer, int size);
void get_log_dir(char* buffer, int size);
void get_dll_dir(char* buffer, int size);
void set_config_dir_override(const char* path);
void log_line(const char* function, int source_line, const char* format, ...);
void log_error(const char* function, int source_line, const char* format, ...);
void puts_help(const char** help_pairs, int count);
void cpy_path_as_abs(char* abs, const char* rel, int abs_size);
int  hash_string(const char* str);
void disable_log();

#define LOG_INFO(...)   log_line(__FUNCTION__, __LINE__, __VA_ARGS__)
#define LOG_ERROR(...)  log_error(__FUNCTION__, __LINE__, __VA_ARGS__)

#define AS_STR(x)       AS_STR_IMPL(x)
#define AS_STR_IMPL(x)  #x

#define sizeof_array(x) (sizeof((x)) / sizeof((x)[0]))

#endif // UTIL_H

// vim: expandtab
