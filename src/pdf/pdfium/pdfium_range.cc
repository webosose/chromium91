// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "pdf/pdfium/pdfium_range.h"

#include "base/check_op.h"
#include "base/strings/string_util.h"
#include "pdf/pdfium/pdfium_api_string_buffer_adapter.h"
#include "ui/gfx/geometry/point.h"
#include "ui/gfx/geometry/rect.h"

namespace chrome_pdf {

namespace {

void AdjustForBackwardsRange(int* index, int* count) {
  int& char_index = *index;
  int& char_count = *count;
  if (char_count < 0) {
    char_count *= -1;
    char_index -= char_count - 1;
  }
}

}  // namespace

bool IsIgnorableCharacter(char16_t c) {
  return (c == kZeroWidthSpace) || (c == kPDFSoftHyphenMarker);
}

PDFiumRange::PDFiumRange(PDFiumPage* page, int char_index, int char_count)
    : page_(page), char_index_(char_index), char_count_(char_count) {
#if DCHECK_IS_ON()
  AdjustForBackwardsRange(&char_index, &char_count);
  DCHECK_LE(char_count, FPDFText_CountChars(page_->GetTextPage()));
#endif
}

PDFiumRange::PDFiumRange(const PDFiumRange& that) = default;

PDFiumRange::~PDFiumRange() = default;

void PDFiumRange::SetCharCount(int char_count) {
  char_count_ = char_count;
#if DCHECK_IS_ON()
  int dummy_index = 0;
  AdjustForBackwardsRange(&dummy_index, &char_count);
  DCHECK_LE(char_count, FPDFText_CountChars(page_->GetTextPage()));
#endif

  cached_screen_rects_point_ = gfx::Point();
  cached_screen_rects_zoom_ = 0;
}

const std::vector<gfx::Rect>& PDFiumRange::GetScreenRects(
    const gfx::Point& point,
    double zoom,
    PageOrientation orientation) const {
  if (point == cached_screen_rects_point_ &&
      zoom == cached_screen_rects_zoom_) {
    return cached_screen_rects_;
  }

  cached_screen_rects_.clear();
  cached_screen_rects_point_ = point;
  cached_screen_rects_zoom_ = zoom;

  int char_index = char_index_;
  int char_count = char_count_;
  if (char_count == 0)
    return cached_screen_rects_;

  AdjustForBackwardsRange(&char_index, &char_count);
  DCHECK_GE(char_index, 0) << " start: " << char_index_
                           << " count: " << char_count_;
  DCHECK_LT(char_index, FPDFText_CountChars(page_->GetTextPage()))
      << " start: " << char_index_ << " count: " << char_count_;

  int count = FPDFText_CountRects(page_->GetTextPage(), char_index, char_count);
  for (int i = 0; i < count; ++i) {
    double left;
    double top;
    double right;
    double bottom;
    FPDFText_GetRect(page_->GetTextPage(), i, &left, &top, &right, &bottom);
    gfx::Rect rect =
        page_->PageToScreen(point, zoom, left, top, right, bottom, orientation);
    if (rect.IsEmpty())
      continue;
    cached_screen_rects_.push_back(rect);
  }

  return cached_screen_rects_;
}

std::u16string PDFiumRange::GetText() const {
  int index = char_index_;
  int count = char_count_;
  std::u16string rv;
  if (count == 0)
    return rv;

  AdjustForBackwardsRange(&index, &count);
  if (count > 0) {
    PDFiumAPIStringBufferAdapter<std::u16string> api_string_adapter(&rv, count,
                                                                    false);
    unsigned short* data =
        reinterpret_cast<unsigned short*>(api_string_adapter.GetData());
    int written = FPDFText_GetText(page_->GetTextPage(), index, count, data);
    api_string_adapter.Close(written);
  }

  base::EraseIf(rv, [](char16_t c) { return IsIgnorableCharacter(c); });

  return rv;
}

}  // namespace chrome_pdf
