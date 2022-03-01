// Copyright (c) 2022 Christopher Antos
// License: http://opensource.org/licenses/MIT

#pragma once

//------------------------------------------------------------------------------
enum class reclassify_reason : unsigned char
{
    recognizer,
    force,
};

//------------------------------------------------------------------------------
void host_reclassify(reclassify_reason why);
