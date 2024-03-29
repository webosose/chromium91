// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_SHARED_HIGHLIGHTING_CORE_COMMON_TEXT_FRAGMENTS_UTILS_H_
#define COMPONENTS_SHARED_HIGHLIGHTING_CORE_COMMON_TEXT_FRAGMENTS_UTILS_H_

#include <vector>

#include "base/values.h"
#include "url/gurl.h"

namespace shared_highlighting {

class TextFragment;

// This file contains helper functions relating to Text Fragments, which are
// appended to the reference fragment in the URL and instruct the user agent
// to highlight a given snippet of text and the page and scroll it into view.
// See also: https://wicg.github.io/scroll-to-text-fragment/

// Checks the fragment portion of the URL for Text Fragments. Returns zero or
// more dictionaries containing the parsed parameters used by the fragment-
// finding algorithm, as defined in the spec.J
base::Value ParseTextFragments(const GURL& url);

// Extracts the text fragments, if any, from a ref string.
std::vector<std::string> ExtractTextFragments(std::string ref_string);

// Remove the text fragment selectors, if any, from url.
GURL RemoveTextFragments(const GURL& url);

// Appends a set of text |fragments| with the correct format to the given
// |base_url|. Returns an empty GURL if |base_url| is invalid.
GURL AppendFragmentDirectives(const GURL& base_url,
                              std::vector<TextFragment> fragments);

}  // namespace shared_highlighting

#endif  // COMPONENTS_SHARED_HIGHLIGHTING_CORE_COMMON_TEXT_FRAGMENTS_UTILS_H_
