// Copyright (c) 2022 Christopher Antos
// License: http://opensource.org/licenses/MIT

#pragma once

//------------------------------------------------------------------------------
enum class reclassify_reason : uint8
{
    recognizer,
    force,
};

//------------------------------------------------------------------------------
void reclassify(reclassify_reason why);
