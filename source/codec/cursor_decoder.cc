//
// Aspia Project
// Copyright (C) 2018 Dmitry Chapyshev <dmitry@aspia.ru>
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program. If not, see <https://www.gnu.org/licenses/>.
//

#include "codec/cursor_decoder.h"

#include "base/logging.h"
#include "codec/decompressor_zstd.h"
#include "desktop_capture/mouse_cursor_cache.h"

namespace aspia {

CursorDecoder::CursorDecoder()
    : decompressor_(std::make_unique<DecompressorZstd>())
{
    // Nothing
}

CursorDecoder::~CursorDecoder() = default;

bool CursorDecoder::decompressCursor(const proto::desktop::CursorShape& cursor_shape,
                                     uint8_t* output,
                                     size_t output_size)
{
    if (cursor_shape.data().empty())
        return false;

    const uint8_t* input = reinterpret_cast<const uint8_t*>(cursor_shape.data().data());
    const size_t input_size = cursor_shape.data().size();

    bool decompress_again = true;
    size_t used = 0;
    size_t filled = 0;

    while (decompress_again)
    {
        size_t written = 0;
        size_t consumed = 0;

        decompress_again = decompressor_->process(input + used,
                                                  input_size - used,
                                                  output + filled,
                                                  output_size - filled,
                                                  &consumed,
                                                  &written);
        used += consumed;
        filled += written;
    }

    decompressor_->reset();
    return true;
}

std::shared_ptr<MouseCursor> CursorDecoder::decode(const proto::desktop::CursorShape& cursor_shape)
{
    size_t cache_index;

    if (cursor_shape.flags() & proto::desktop::CursorShape::CACHE)
    {
        // Bits 0-4 contain the cursor position in the cache.
        cache_index = cursor_shape.flags() & 0x1F;
    }
    else
    {
        DesktopSize size(cursor_shape.width(), cursor_shape.height());

        if (size.width()  <= 0 || size.width()  > (std::numeric_limits<int16_t>::max() / 2) ||
            size.height() <= 0 || size.height() > (std::numeric_limits<int16_t>::max() / 2))
        {
            LOG(LS_WARNING) << "Cursor dimensions are out of bounds for SetCursor: "
                            << size.width() << "x" << size.height();
            return nullptr;
        }

        size_t image_size = size.width() * size.height() * sizeof(uint32_t);
        std::unique_ptr<uint8_t[]> image = std::make_unique<uint8_t[]>(image_size);

        if (!decompressCursor(cursor_shape, image.get(), image_size))
            return nullptr;

        std::unique_ptr<MouseCursor> mouse_cursor = std::make_unique<MouseCursor>(
            std::move(image), size, DesktopPoint(cursor_shape.hotspot_x(), cursor_shape.hotspot_y()));

        if (cursor_shape.flags() & proto::desktop::CursorShape::RESET_CACHE)
        {
            size_t cache_size = cursor_shape.flags() & 0x1F;

            if (!MouseCursorCache::isValidCacheSize(cache_size))
                return nullptr;

            cache_ = std::make_unique<MouseCursorCache>(cache_size);
        }

        if (!cache_)
        {
            LOG(LS_WARNING) << "Host did not send cache reset command";
            return nullptr;
        }

        cache_index = cache_->add(std::move(mouse_cursor));
    }

    return cache_->get(cache_index);
}

} // namespace aspia
