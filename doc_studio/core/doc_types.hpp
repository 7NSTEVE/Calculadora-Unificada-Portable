#pragma once

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace DocStudio {

struct DocPoint {
    double x = 0.0;
    double y = 0.0;
};

struct InkPoint {
    double x = 0.0;
    double y = 0.0;
    float pressure = 0.5f; // normalized 0.0 .. 1.0
};

struct DocRect {
    double x = 0.0;
    double y = 0.0;
    double w = 0.0;
    double h = 0.0;

    double Right() const noexcept { return x + w; }
    double Bottom() const noexcept { return y + h; }
    bool Empty() const noexcept { return w <= 0.0 || h <= 0.0; }

    bool Intersects(const DocRect& o) const noexcept {
        return !(Right() < o.x || o.Right() < x || Bottom() < o.y || o.Bottom() < y);
    }

    bool Contains(double px, double py, double margin = 0.0) const noexcept {
        return px >= (x - margin) && px <= (Right() + margin) &&
               py >= (y - margin) && py <= (Bottom() + margin);
    }

    static DocRect FromMinMax(double minX, double minY, double maxX, double maxY) noexcept {
        return { minX, minY, (std::max)(0.0, maxX - minX), (std::max)(0.0, maxY - minY) };
    }
};

struct DocColor {
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;
    std::uint8_t a = 255;

    static constexpr DocColor RGBA(std::uint8_t rr, std::uint8_t gg, std::uint8_t bb, std::uint8_t aa = 255) noexcept {
        return { rr, gg, bb, aa };
    }

    std::uint32_t PackRgba() const noexcept {
        return (static_cast<std::uint32_t>(r) << 24) |
               (static_cast<std::uint32_t>(g) << 16) |
               (static_cast<std::uint32_t>(b) << 8) |
               static_cast<std::uint32_t>(a);
    }

    static constexpr DocColor UnpackRgba(std::uint32_t v) noexcept {
        return {
            static_cast<std::uint8_t>((v >> 24) & 0xFFu),
            static_cast<std::uint8_t>((v >> 16) & 0xFFu),
            static_cast<std::uint8_t>((v >> 8) & 0xFFu),
            static_cast<std::uint8_t>(v & 0xFFu)
        };
    }
};

enum class DocumentMode : std::uint8_t {
    PaginatedPdf = 0,
    InfiniteCanvas = 1
};

enum class PaperPattern : std::uint8_t {
    Plain = 0,
    Ruled = 1,
    Grid = 2,
    Dots = 3,
    Millimeter = 4
};

enum class ObjectKind : std::uint8_t {
    InkStroke = 0,
    Shape = 1,
    TextBox = 2,
    Image = 3,
    PdfCard = 4
};

enum class ShapeKind : std::uint8_t {
    Line = 0,
    Arrow = 1,
    Rectangle = 2,
    Ellipse = 3,
    SignatureStamp = 4
};

struct AnnotationObject {
    std::uint64_t id = 0;
    std::uint64_t zOrder = 0;
    ObjectKind kind = ObjectKind::InkStroke;
    DocRect bounds{};

    // Appearance
    DocColor strokeColor = DocColor::RGBA(26, 115, 232, 255);
    DocColor fillColor = DocColor::RGBA(0, 0, 0, 0);
    double strokeWidth = 2.5;
    bool isHighlighter = false;

    // InkStroke
    std::vector<InkPoint> inkPoints;

    // Shape (also stores endpoints p0->p1 in shapeStart/shapeEnd for line/arrow)
    ShapeKind shapeKind = ShapeKind::Rectangle;
    DocPoint shapeStart{};
    DocPoint shapeEnd{};

    // TextBox
    std::string textUtf8;
    std::string fontFamilyUtf8 = "Segoe UI";
    double fontSizePt = 14.0;
    bool bold = false;
    bool italic = false;

    // Image (PNG bytes stored in DocumentModel::assets[assetKey] or inline pngBytes)
    std::string assetKey;
    std::vector<std::uint8_t> pngBytes;
    int imagePixelW = 0;
    int imagePixelH = 0;
    std::string captionUtf8;

    // PdfCard (used on InfiniteCanvas when importing a PDF page)
    std::string pdfSourcePathUtf8;
    int pdfSourcePageIndex0 = 0;

    void RecomputeBounds() noexcept {
        if (kind == ObjectKind::InkStroke) {
            if (inkPoints.empty()) {
                bounds = {};
                return;
            }
            double minX = inkPoints[0].x, maxX = inkPoints[0].x;
            double minY = inkPoints[0].y, maxY = inkPoints[0].y;
            for (const auto& pt : inkPoints) {
                if (pt.x < minX) minX = pt.x;
                if (pt.x > maxX) maxX = pt.x;
                if (pt.y < minY) minY = pt.y;
                if (pt.y > maxY) maxY = pt.y;
            }
            const double pad = (std::max)(2.0, strokeWidth);
            bounds = { minX - pad, minY - pad, (maxX - minX) + pad * 2.0, (maxY - minY) + pad * 2.0 };
        } else if (kind == ObjectKind::Shape && (shapeKind == ShapeKind::Line || shapeKind == ShapeKind::Arrow)) {
            const double minX = (std::min)(shapeStart.x, shapeEnd.x);
            const double minY = (std::min)(shapeStart.y, shapeEnd.y);
            const double maxX = (std::max)(shapeStart.x, shapeEnd.x);
            const double maxY = (std::max)(shapeStart.y, shapeEnd.y);
            const double pad = (std::max)(4.0, strokeWidth * 2.0);
            bounds = { minX - pad, minY - pad, (maxX - minX) + pad * 2.0, (maxY - minY) + pad * 2.0 };
        }
    }
};

struct NormalizedCropRect {
    double left = 0.0;
    double top = 0.0;
    double right = 1.0;
    double bottom = 1.0;

    bool IsFull() const noexcept {
        return left <= 0.0001 && top <= 0.0001 && right >= 0.9999 && bottom >= 0.9999;
    }

    void Clamp() noexcept {
        left = std::clamp(left, 0.0, 0.95);
        top = std::clamp(top, 0.0, 0.95);
        right = std::clamp(right, left + 0.05, 1.0);
        bottom = std::clamp(bottom, top + 0.05, 1.0);
    }
};

struct PageModel {
    std::uint64_t pageId = 0;
    double widthPt = 595.0;  // A4 width (pt)
    double heightPt = 842.0; // A4 height (pt)
    int rotationDeg = 0;     // 0, 90, 180, 270
    NormalizedCropRect crop{};
    PaperPattern background = PaperPattern::Plain;

    // If sourced from an existing PDF file
    bool hasPdfSource = false;
    std::string sourcePdfPathUtf8;
    int sourcePageIndex0 = 0;

    // Optional cached OCR or extracted plain text
    std::string extractedTextUtf8;
    std::string ocrTextUtf8;

    std::vector<AnnotationObject> objects;

    double EffectiveWidthPt() const noexcept {
        const int r = ((rotationDeg % 360) + 360) % 360;
        const double baseW = (r == 90 || r == 270) ? heightPt : widthPt;
        return baseW * (std::max)(0.05, crop.right - crop.left);
    }

    double EffectiveHeightPt() const noexcept {
        const int r = ((rotationDeg % 360) + 360) % 360;
        const double baseH = (r == 90 || r == 270) ? widthPt : heightPt;
        return baseH * (std::max)(0.05, crop.bottom - crop.top);
    }
};

struct SearchMatch {
    std::size_t pageIndex = 0;
    std::size_t charOffset = 0;
    std::size_t charLength = 0;
    std::string snippetUtf8;
    bool hasBox = false;
    DocRect boxNormalized{}; // [0..1] in page coordinates if layout box matched
};

} // namespace DocStudio
