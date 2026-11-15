#include "pdf_writer.hpp"
#include "../core/utf_conv.hpp"

#include <zlib.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <iomanip>
#include <map>
#include <sstream>
#include <unordered_map>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>

namespace DocStudio {

namespace {

using Microsoft::WRL::ComPtr;

std::string FmtNum(double v) {
    if (std::abs(v) < 1e-6) return "0";
    char buf[64]{};
    std::snprintf(buf, sizeof(buf), "%.3f", v);
    std::string s(buf);
    while (s.find('.') != std::string::npos && s.back() == '0') s.pop_back();
    if (!s.empty() && s.back() == '.') s.pop_back();
    return s.empty() ? "0" : s;
}

std::string EscapePdfLiteralString(const std::string& asciiOrWinAnsi) {
    std::string out = "(";
    for (unsigned char ch : asciiOrWinAnsi) {
        if (ch == '(' || ch == ')' || ch == '\\') {
            out.push_back('\\');
            out.push_back(static_cast<char>(ch));
        } else if (ch >= 0x20 && ch <= 0x7Eu) {
            out.push_back(static_cast<char>(ch));
        } else {
            char oct[8]{};
            std::snprintf(oct, sizeof(oct), "\\%03o", ch);
            out += oct;
        }
    }
    out.push_back(')');
    return out;
}

// Converts UTF-8 to WinAnsi bytes for built-in PDF Helvetica font
std::string Utf8ToWinAnsi(const std::string& utf8) {
    const std::wstring wide = Utf8ToWide(utf8);
    if (wide.empty()) return {};
    const int n = WideCharToMultiByte(1252, 0, wide.data(), static_cast<int>(wide.size()), nullptr, 0, "?", nullptr);
    if (n <= 0) return {};
    std::string out(static_cast<std::size_t>(n), '\0');
    WideCharToMultiByte(1252, 0, wide.data(), static_cast<int>(wide.size()), out.data(), n, "?", nullptr);
    return out;
}

bool DeflateBytes(const std::uint8_t* src, std::size_t len, std::vector<std::uint8_t>& out) {
    uLongf bound = compressBound(static_cast<uLong>(len));
    out.resize(bound);
    if (compress2(out.data(), &bound, src, static_cast<uLong>(len), Z_DEFAULT_COMPRESSION) != Z_OK) {
        out.clear();
        return false;
    }
    out.resize(bound);
    return true;
}

bool DecodeImageBytesViaWic(
    const std::vector<std::uint8_t>& encodedBytes,
    int& outW,
    int& outH,
    std::vector<std::uint8_t>& outRgb,
    std::vector<std::uint8_t>& outAlpha)
{
    if (encodedBytes.empty()) return false;
    const HRESULT hrCo = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool didInit = SUCCEEDED(hrCo);

    bool ok = false;
    {
        ComPtr<IWICImagingFactory> factory;
        if (SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)))) {
            ComPtr<IWICStream> stream;
            if (SUCCEEDED(factory->CreateStream(&stream)) &&
                SUCCEEDED(stream->InitializeFromMemory(const_cast<BYTE*>(encodedBytes.data()), static_cast<DWORD>(encodedBytes.size())))) {
                ComPtr<IWICBitmapDecoder> decoder;
                if (SUCCEEDED(factory->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnDemand, &decoder))) {
                    ComPtr<IWICBitmapFrameDecode> frame;
                    if (SUCCEEDED(decoder->GetFrame(0, &frame))) {
                        UINT w = 0, h = 0;
                        if (SUCCEEDED(frame->GetSize(&w, &h)) && w > 0 && h > 0 && w <= 8192 && h <= 8192) {
                            ComPtr<IWICFormatConverter> conv;
                            if (SUCCEEDED(factory->CreateFormatConverter(&conv)) &&
                                SUCCEEDED(conv->Initialize(frame.Get(), GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom))) {
                                std::vector<std::uint8_t> rgba(static_cast<std::size_t>(w) * h * 4u);
                                if (SUCCEEDED(conv->CopyPixels(nullptr, w * 4u, static_cast<UINT>(rgba.size()), rgba.data()))) {
                                    outW = static_cast<int>(w);
                                    outH = static_cast<int>(h);
                                    outRgb.resize(static_cast<std::size_t>(w) * h * 3u);
                                    outAlpha.resize(static_cast<std::size_t>(w) * h);
                                    bool hasPartialAlpha = false;
                                    for (std::size_t i = 0, p = static_cast<std::size_t>(w) * h; i < p; ++i) {
                                        outRgb[i * 3 + 0] = rgba[i * 4 + 0];
                                        outRgb[i * 3 + 1] = rgba[i * 4 + 1];
                                        outRgb[i * 3 + 2] = rgba[i * 4 + 2];
                                        const std::uint8_t a = rgba[i * 4 + 3];
                                        outAlpha[i] = a;
                                        if (a != 255) hasPartialAlpha = true;
                                    }
                                    if (!hasPartialAlpha) outAlpha.clear();
                                    ok = true;
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    if (didInit) CoUninitialize();
    return ok;
}

struct PdfBuilder {
    struct Entry {
        std::uint32_t id = 0;
        std::string body; // everything between "<id> 0 obj\n" and "\nendobj"
    };

    std::vector<Entry> objects;

    std::uint32_t AllocId() {
        objects.push_back({ static_cast<std::uint32_t>(objects.size() + 1), {} });
        return objects.back().id;
    }

    void SetObject(std::uint32_t id, std::string body) {
        if (id >= 1 && id <= objects.size()) {
            objects[id - 1].body = std::move(body);
        }
    }

    std::uint32_t AddObject(std::string body) {
        const std::uint32_t id = AllocId();
        SetObject(id, std::move(body));
        return id;
    }

    std::uint32_t AddStream(const std::string& dictEntriesWithoutLength, const std::vector<std::uint8_t>& rawData, bool compress = true) {
        std::vector<std::uint8_t> payload;
        bool isCompressed = false;
        if (compress && !rawData.empty()) {
            isCompressed = DeflateBytes(rawData.data(), rawData.size(), payload);
        }
        const std::vector<std::uint8_t>& finalBytes = isCompressed ? payload : rawData;

        std::ostringstream hdr;
        hdr << "<<" << dictEntriesWithoutLength;
        if (isCompressed) hdr << "/Filter /FlateDecode ";
        hdr << "/Length " << finalBytes.size() << ">>\nstream\n";
        std::string body = hdr.str();
        body.append(reinterpret_cast<const char*>(finalBytes.data()), finalBytes.size());
        body += "\nendstream";
        return AddObject(std::move(body));
    }

    std::uint32_t AddStandardHelveticaFont(bool bold = false) {
        const char* base = bold ? "/Helvetica-Bold" : "/Helvetica";
        return AddObject(
            std::string("<</Type /Font /Subtype /Type1 /BaseFont ") + base +
            " /Encoding /WinAnsiEncoding>>");
    }

    std::uint32_t AddHighlightExtGState(double alpha = 0.36) {
        return AddObject("<</Type /ExtGState /ca " + FmtNum(alpha) + " /CA " + FmtNum(alpha) + ">>");
    }

    std::uint32_t AddImageXObject(const std::vector<std::uint8_t>& encodedBytes) {
        int w = 0, h = 0;
        std::vector<std::uint8_t> rgb, alpha;
        if (!DecodeImageBytesViaWic(encodedBytes, w, h, rgb, alpha)) return 0;

        std::uint32_t smaskId = 0;
        if (!alpha.empty()) {
            smaskId = AddStream(
                "/Type /XObject /Subtype /Image /Width " + std::to_string(w) +
                " /Height " + std::to_string(h) +
                " /ColorSpace /DeviceGray /BitsPerComponent 8 ",
                alpha, true);
        }

        std::ostringstream dict;
        dict << "/Type /XObject /Subtype /Image /Width " << w << " /Height " << h
             << " /ColorSpace /DeviceRGB /BitsPerComponent 8 ";
        if (smaskId != 0) dict << "/SMask " << smaskId << " 0 R ";
        return AddStream(dict.str(), rgb, true);
    }

    // Deeply clones an object graph from a parsed PdfDocument into this PdfBuilder.
    std::uint32_t CloneObjectFromPdf(
        const PdfDocument& src,
        std::uint32_t srcObjNum,
        std::unordered_map<std::uint32_t, std::uint32_t>& remap,
        int depth = 0)
    {
        auto it = remap.find(srcObjNum);
        if (it != remap.end()) return it->second;
        if (depth > 48) return 0;

        const std::uint32_t newId = AllocId();
        remap[srcObjNum] = newId;

        PdfValue v = src.LookupObject(srcObjNum);
        PdfValue cloned = RewriteValueRefs(src, v, remap, depth + 1);
        SetObject(newId, PdfDocument::SerializeValue(cloned));
        return newId;
    }

    PdfValue RewriteValueRefs(
        const PdfDocument& src,
        const PdfValue& v,
        std::unordered_map<std::uint32_t, std::uint32_t>& remap,
        int depth)
    {
        PdfValue out = v;
        if (v.kind == PdfValue::Kind::Ref) {
            const std::uint32_t mapped = CloneObjectFromPdf(src, v.refVal.obj, remap, depth + 1);
            out.refVal = { mapped, 0 };
        } else if (v.kind == PdfValue::Kind::Array) {
            for (auto& elem : out.arrayVal) {
                elem = RewriteValueRefs(src, elem, remap, depth);
            }
        } else if (v.kind == PdfValue::Kind::Dict) {
            for (auto& kv : out.dictVal) {
                kv.second = RewriteValueRefs(src, kv.second, remap, depth);
            }
        } else if (v.kind == PdfValue::Kind::Stream && v.streamVal) {
            auto sd = std::make_shared<PdfStreamData>();
            sd->rawBytes = v.streamVal->rawBytes;
            for (const auto& kv : v.streamVal->dict) {
                sd->dict[kv.first] = RewriteValueRefs(src, kv.second, remap, depth);
            }
            out.streamVal = std::move(sd);
        }
        return out;
    }

    std::vector<std::uint8_t> Finalize(std::uint32_t catalogId) const {
        std::string out = "%PDF-1.7\n%\xE2\xE3\xCF\xD3\n";
        std::vector<std::size_t> offsets(objects.size() + 1, 0);
        for (const auto& obj : objects) {
            offsets[obj.id] = out.size();
            out += std::to_string(obj.id) + " 0 obj\n";
            out += obj.body;
            out += "\nendobj\n";
        }
        const std::size_t xrefOffset = out.size();
        out += "xref\n0 " + std::to_string(objects.size() + 1) + "\n";
        out += "0000000000 65535 f \n";
        for (std::size_t i = 1; i <= objects.size(); ++i) {
            char line[32]{};
            std::snprintf(line, sizeof(line), "%010zu 00000 n \n", offsets[i]);
            out += line;
        }
        out += "trailer\n<</Size " + std::to_string(objects.size() + 1) +
               " /Root " + std::to_string(catalogId) + " 0 R>>\n";
        out += "startxref\n" + std::to_string(xrefOffset) + "\n%%EOF\n";
        return std::vector<std::uint8_t>(out.begin(), out.end());
    }
};

void EmitPaperPatternOps(std::ostringstream& ss, double w, double h, PaperPattern bg) {
    if (bg == PaperPattern::Plain) return;

    ss << "q\n";
    if (bg == PaperPattern::Ruled) {
        ss << "0.82 0.88 0.96 RG 0.6 w\n";
        for (double y = 36.0; y < h - 18.0; y += 24.0) {
            ss << "0 " << FmtNum(y) << " m " << FmtNum(w) << " " << FmtNum(y) << " l S\n";
        }
        ss << "0.94 0.60 0.62 RG 0.8 w 56 0 m 56 " << FmtNum(h) << " l S\n";
    } else if (bg == PaperPattern::Grid) {
        ss << "0.86 0.90 0.95 RG 0.5 w\n";
        for (double x = 20.0; x < w; x += 20.0) {
            ss << FmtNum(x) << " 0 m " << FmtNum(x) << " " << FmtNum(h) << " l S\n";
        }
        for (double y = 20.0; y < h; y += 20.0) {
            ss << "0 " << FmtNum(y) << " m " << FmtNum(w) << " " << FmtNum(y) << " l S\n";
        }
    } else if (bg == PaperPattern::Dots) {
        ss << "0.70 0.74 0.80 rg\n";
        for (double y = 20.0; y < h; y += 20.0) {
            for (double x = 20.0; x < w; x += 20.0) {
                ss << FmtNum(x - 0.7) << " " << FmtNum(y - 0.7) << " 1.4 1.4 re f\n";
            }
        }
    } else if (bg == PaperPattern::Millimeter) {
        ss << "0.90 0.94 0.91 RG 0.25 w\n";
        for (double x = 8.0; x < w; x += 8.0) {
            ss << FmtNum(x) << " 0 m " << FmtNum(x) << " " << FmtNum(h) << " l S\n";
        }
        for (double y = 8.0; y < h; y += 8.0) {
            ss << "0 " << FmtNum(y) << " m " << FmtNum(w) << " " << FmtNum(y) << " l S\n";
        }
        ss << "0.72 0.85 0.76 RG 0.6 w\n";
        for (double x = 40.0; x < w; x += 40.0) {
            ss << FmtNum(x) << " 0 m " << FmtNum(x) << " " << FmtNum(h) << " l S\n";
        }
        for (double y = 40.0; y < h; y += 40.0) {
            ss << "0 " << FmtNum(y) << " m " << FmtNum(w) << " " << FmtNum(y) << " l S\n";
        }
    }
    ss << "Q\n";
}

void EmitRgbStrokeColor(std::ostringstream& ss, DocColor c) {
    ss << FmtNum(c.r / 255.0) << ' ' << FmtNum(c.g / 255.0) << ' ' << FmtNum(c.b / 255.0) << " RG ";
}

void EmitRgbFillColor(std::ostringstream& ss, DocColor c) {
    ss << FmtNum(c.r / 255.0) << ' ' << FmtNum(c.g / 255.0) << ' ' << FmtNum(c.b / 255.0) << " rg ";
}

void EmitAnnotationObjectOps(
    std::ostringstream& ss,
    const AnnotationObject& obj,
    const DocumentModel& doc,
    PdfBuilder& builder,
    std::map<std::string, std::uint32_t>& imageXObjects,
    double offsetX,
    double offsetY,
    bool flipTextYInCurrentCtm)
{
    auto tx = [&](double x) { return x + offsetX; };
    auto ty = [&](double y) { return y + offsetY; };

    ss << "q\n";
    if (obj.isHighlighter || obj.strokeColor.a < 220) {
        ss << "/GSAlpha gs\n";
    }

    if (obj.kind == ObjectKind::InkStroke) {
        if (obj.inkPoints.size() == 1) {
            EmitRgbFillColor(ss, obj.strokeColor);
            const double r = (std::max)(1.0, obj.strokeWidth * 0.5);
            ss << FmtNum(tx(obj.inkPoints[0].x) - r) << ' ' << FmtNum(ty(obj.inkPoints[0].y) - r)
               << ' ' << FmtNum(r * 2.0) << ' ' << FmtNum(r * 2.0) << " re f\n";
        } else if (obj.inkPoints.size() >= 2) {
            EmitRgbStrokeColor(ss, obj.strokeColor);
            ss << "1 J 1 j\n";
            // Average pressure modulation or segment chunks
            for (std::size_t i = 1; i < obj.inkPoints.size(); ++i) {
                const auto& p0 = obj.inkPoints[i - 1];
                const auto& p1 = obj.inkPoints[i];
                const double avgP = 0.5 * (p0.pressure + p1.pressure);
                const double w = obj.isHighlighter
                    ? (std::max)(4.0, obj.strokeWidth)
                    : (std::max)(0.6, obj.strokeWidth * (0.45 + 1.1 * avgP));
                ss << FmtNum(w) << " w "
                   << FmtNum(tx(p0.x)) << ' ' << FmtNum(ty(p0.y)) << " m "
                   << FmtNum(tx(p1.x)) << ' ' << FmtNum(ty(p1.y)) << " l S\n";
            }
        }
    } else if (obj.kind == ObjectKind::Shape) {
        EmitRgbStrokeColor(ss, obj.strokeColor);
        ss << FmtNum((std::max)(0.5, obj.strokeWidth)) << " w\n";
        const DocRect r = { tx(obj.bounds.x), ty(obj.bounds.y), obj.bounds.w, obj.bounds.h };

        if (obj.shapeKind == ShapeKind::Rectangle || obj.shapeKind == ShapeKind::SignatureStamp) {
            if (obj.fillColor.a > 0) {
                EmitRgbFillColor(ss, obj.fillColor);
                ss << FmtNum(r.x) << ' ' << FmtNum(r.y) << ' ' << FmtNum(r.w) << ' ' << FmtNum(r.h) << " re B\n";
            } else {
                ss << FmtNum(r.x) << ' ' << FmtNum(r.y) << ' ' << FmtNum(r.w) << ' ' << FmtNum(r.h) << " re S\n";
            }
            if (obj.shapeKind == ShapeKind::SignatureStamp && !obj.textUtf8.empty()) {
                EmitRgbFillColor(ss, obj.strokeColor);
                const std::string txt = EscapePdfLiteralString(Utf8ToWinAnsi(obj.textUtf8));
                const double fs = (std::max)(8.0, obj.fontSizePt > 0.0 ? obj.fontSizePt : 10.0);
                ss << "BT /F2 " << FmtNum(fs) << " Tf ";
                if (flipTextYInCurrentCtm) {
                    ss << "1 0 0 -1 " << FmtNum(r.x + 6.0) << ' ' << FmtNum(r.y + fs + 4.0) << " Tm ";
                } else {
                    ss << "1 0 0 1 " << FmtNum(r.x + 6.0) << ' ' << FmtNum(r.y + r.h * 0.5) << " Tm ";
                }
                ss << txt << " Tj ET\n";
            }
        } else if (obj.shapeKind == ShapeKind::Ellipse) {
            // 4-segment cubic Bezier approximation for ellipse
            const double cx = r.x + r.w * 0.5;
            const double cy = r.y + r.h * 0.5;
            const double rx = r.w * 0.5;
            const double ry = r.h * 0.5;
            const double kx = rx * 0.55228475;
            const double ky = ry * 0.55228475;
            ss << FmtNum(cx + rx) << ' ' << FmtNum(cy) << " m\n"
               << FmtNum(cx + rx) << ' ' << FmtNum(cy + ky) << ' ' << FmtNum(cx + kx) << ' ' << FmtNum(cy + ry) << ' ' << FmtNum(cx) << ' ' << FmtNum(cy + ry) << " c\n"
               << FmtNum(cx - kx) << ' ' << FmtNum(cy + ry) << ' ' << FmtNum(cx - rx) << ' ' << FmtNum(cy + ky) << ' ' << FmtNum(cx - rx) << ' ' << FmtNum(cy) << " c\n"
               << FmtNum(cx - rx) << ' ' << FmtNum(cy - ky) << ' ' << FmtNum(cx - kx) << ' ' << FmtNum(cy - ry) << ' ' << FmtNum(cx) << ' ' << FmtNum(cy - ry) << " c\n"
               << FmtNum(cx + kx) << ' ' << FmtNum(cy - ry) << ' ' << FmtNum(cx + rx) << ' ' << FmtNum(cy - ky) << ' ' << FmtNum(cx + rx) << ' ' << FmtNum(cy) << " c\n";
            if (obj.fillColor.a > 0) {
                EmitRgbFillColor(ss, obj.fillColor);
                ss << "B\n";
            } else {
                ss << "S\n";
            }
        } else {
            // Line / Arrow
            const double x0 = tx(obj.shapeStart.x), y0 = ty(obj.shapeStart.y);
            const double x1 = tx(obj.shapeEnd.x), y1 = ty(obj.shapeEnd.y);
            ss << "1 J " << FmtNum(x0) << ' ' << FmtNum(y0) << " m " << FmtNum(x1) << ' ' << FmtNum(y1) << " l S\n";
            if (obj.shapeKind == ShapeKind::Arrow) {
                const double dx = x1 - x0;
                const double dy = y1 - y0;
                const double len = std::hypot(dx, dy);
                if (len > 1e-3) {
                    const double ux = dx / len, uy = dy / len;
                    const double hs = (std::max)(8.0, obj.strokeWidth * 3.2);
                    const double px = -uy * (hs * 0.45), py = ux * (hs * 0.45);
                    ss << FmtNum(x1 - ux * hs + px) << ' ' << FmtNum(y1 - uy * hs + py) << " m "
                       << FmtNum(x1) << ' ' << FmtNum(y1) << " l "
                       << FmtNum(x1 - ux * hs - px) << ' ' << FmtNum(y1 - uy * hs - py) << " l S\n";
                }
            }
        }
    } else if (obj.kind == ObjectKind::TextBox) {
        const DocRect r = { tx(obj.bounds.x), ty(obj.bounds.y), (std::max)(24.0, obj.bounds.w), (std::max)(18.0, obj.bounds.h) };
        if (obj.fillColor.a > 0) {
            EmitRgbFillColor(ss, obj.fillColor);
            ss << FmtNum(r.x) << ' ' << FmtNum(r.y) << ' ' << FmtNum(r.w) << ' ' << FmtNum(r.h) << " re f\n";
        }
        EmitRgbFillColor(ss, obj.strokeColor);
        const double fs = (std::max)(6.0, obj.fontSizePt);
        const char* fontRes = obj.bold ? "/F2" : "/F1";
        // Split lines on '\n'
        std::istringstream iss(obj.textUtf8);
        std::string line;
        int lineIdx = 0;
        while (std::getline(iss, line)) {
            const std::string lit = EscapePdfLiteralString(Utf8ToWinAnsi(line));
            const double baselineOffset = fs * 0.9 + static_cast<double>(lineIdx) * (fs * 1.25);
            ss << "BT " << fontRes << ' ' << FmtNum(fs) << " Tf ";
            if (flipTextYInCurrentCtm) {
                ss << "1 0 0 -1 " << FmtNum(r.x + 2.0) << ' ' << FmtNum(r.y + baselineOffset) << " Tm ";
            } else {
                ss << "1 0 0 1 " << FmtNum(r.x + 2.0) << ' ' << FmtNum(r.y + r.h - baselineOffset) << " Tm ";
            }
            ss << lit << " Tj ET\n";
            ++lineIdx;
        }
    } else if (obj.kind == ObjectKind::Image) {
        const std::vector<std::uint8_t>* bytesPtr = !obj.pngBytes.empty() ? &obj.pngBytes : doc.FindAsset(obj.assetKey);
        if (bytesPtr && !bytesPtr->empty()) {
            const std::uint32_t xobjId = builder.AddImageXObject(*bytesPtr);
            if (xobjId != 0) {
                const std::string resName = "Im" + std::to_string(imageXObjects.size() + 1);
                imageXObjects[resName] = xobjId;
                const DocRect r = { tx(obj.bounds.x), ty(obj.bounds.y), (std::max)(4.0, obj.bounds.w), (std::max)(4.0, obj.bounds.h) };
                ss << "q " << FmtNum(r.w) << " 0 0 " << FmtNum(-r.h) << ' ' << FmtNum(r.x) << ' ' << FmtNum(r.y + r.h)
                   << " cm /" << resName << " Do Q\n";
            }
        }
    }

    ss << "Q\n";
}

// Builds top-down CTM matrix for a page of cropped source box [L..R, B..T] rotated by rotDeg
std::string BuildPageOverlayCtm(double L, double B, double R, double T, int rotDeg) {
    const int r = ((rotDeg % 360) + 360) % 360;
    std::ostringstream ss;
    if (r == 0) {
        ss << "1 0 0 -1 " << FmtNum(L) << ' ' << FmtNum(T) << " cm\n";
    } else if (r == 90) {
        ss << "0 1 1 0 " << FmtNum(L) << ' ' << FmtNum(B) << " cm\n";
    } else if (r == 180) {
        ss << "-1 0 0 1 " << FmtNum(R) << ' ' << FmtNum(B) << " cm\n";
    } else {
        ss << "0 -1 -1 0 " << FmtNum(R) << ' ' << FmtNum(T) << " cm\n";
    }
    return ss.str();
}

bool WriteAllBytesToFile(const std::wstring& path, const std::vector<std::uint8_t>& bytes, std::string* outErr) {
    const std::wstring tmpPath = path + L".tmp";
    HANDLE hFile = CreateFileW(tmpPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) {
        if (outErr) *outErr = "No se pudo crear archivo PDF destino (WinError " + std::to_string(GetLastError()) + ").";
        return false;
    }
    std::size_t off = 0;
    while (off < bytes.size()) {
        const DWORD chunk = static_cast<DWORD>((std::min<std::size_t>)(bytes.size() - off, 4u * 1024u * 1024u));
        DWORD wr = 0;
        if (!WriteFile(hFile, bytes.data() + off, chunk, &wr, nullptr) || wr != chunk) {
            CloseHandle(hFile);
            DeleteFileW(tmpPath.c_str());
            if (outErr) *outErr = "Error escribiendo PDF.";
            return false;
        }
        off += wr;
    }
    FlushFileBuffers(hFile);
    CloseHandle(hFile);
    if (!MoveFileExW(tmpPath.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(tmpPath.c_str());
        if (outErr) *outErr = "No se pudo finalizar archivo PDF.";
        return false;
    }
    return true;
}

} // namespace

bool PdfWriter::ExportDocumentToBytes(
    const DocumentModel& doc,
    std::vector<std::uint8_t>& outPdfBytes,
    std::string* outErr)
{
    if (doc.Pages().empty()) {
        if (outErr) *outErr = "El documento no tiene p\xC3\xA1ginas para exportar.";
        return false;
    }

    PdfBuilder builder;
    const std::uint32_t f1 = builder.AddStandardHelveticaFont(false);
    const std::uint32_t f2 = builder.AddStandardHelveticaFont(true);
    const std::uint32_t gsAlpha = builder.AddHighlightExtGState(0.38);

    std::unordered_map<std::string, std::shared_ptr<PdfDocument>> loadedSources;
    std::unordered_map<std::string, std::unordered_map<std::uint32_t, std::uint32_t>> sourceRemaps;

    const std::uint32_t pagesRootId = builder.AllocId();
    std::vector<std::uint32_t> pageIds;

    for (const auto& pg : doc.Pages()) {
        std::shared_ptr<PdfDocument> srcDoc;
        if (pg.hasPdfSource && !pg.sourcePdfPathUtf8.empty()) {
            auto sit = loadedSources.find(pg.sourcePdfPathUtf8);
            if (sit == loadedSources.end()) {
                auto loaded = std::make_shared<PdfDocument>();
                std::string dummyErr;
                if (loaded->LoadFromFile(Utf8ToWide(pg.sourcePdfPathUtf8), &dummyErr)) {
                    loadedSources[pg.sourcePdfPathUtf8] = loaded;
                    srcDoc = loaded;
                } else {
                    loadedSources[pg.sourcePdfPathUtf8] = nullptr;
                }
            } else {
                srcDoc = sit->second;
            }
        }

        const bool hasValidSourcePage = srcDoc && pg.sourcePageIndex0 >= 0 &&
            static_cast<std::size_t>(pg.sourcePageIndex0) < srcDoc->PageCount();

        if (hasValidSourcePage) {
            const auto& srcPageInfo = srcDoc->PageAt(static_cast<std::size_t>(pg.sourcePageIndex0));
            const PdfValue srcPageVal = srcDoc->LookupObject(srcPageInfo.pageRef.obj);
            auto& remap = sourceRemaps[pg.sourcePdfPathUtf8];

            const DocRect baseBox = srcPageInfo.hasCropBox ? srcPageInfo.cropBox : srcPageInfo.mediaBox;
            // Apply NormalizedCropRect in unrotated user space
            const double cropL = baseBox.x + baseBox.w * pg.crop.left;
            const double cropR = baseBox.x + baseBox.w * pg.crop.right;
            const double cropT = baseBox.Bottom() - baseBox.h * pg.crop.top;
            const double cropB = baseBox.Bottom() - baseBox.h * pg.crop.bottom;
            const DocRect finalCrop = DocRect::FromMinMax((std::min)(cropL, cropR), (std::min)(cropB, cropT),
                                                          (std::max)(cropL, cropR), (std::max)(cropB, cropT));

            // Clone existing contents and resources
            std::vector<std::uint32_t> clonedContentIds;
            const PdfValue* contents = srcPageVal.DictGet("Contents");
            if (contents) {
                if (contents->IsRef()) {
                    const PdfValue* resolved = srcDoc->Resolve(contents);
                    if (resolved && resolved->IsArray()) {
                        for (const auto& elem : resolved->arrayVal) {
                            if (elem.IsRef()) {
                                clonedContentIds.push_back(builder.CloneObjectFromPdf(*srcDoc, elem.refVal.obj, remap));
                            }
                        }
                    } else {
                        clonedContentIds.push_back(builder.CloneObjectFromPdf(*srcDoc, contents->refVal.obj, remap));
                    }
                } else if (contents->IsArray()) {
                    for (const auto& elem : contents->arrayVal) {
                        if (elem.IsRef()) {
                            clonedContentIds.push_back(builder.CloneObjectFromPdf(*srcDoc, elem.refVal.obj, remap));
                        }
                    }
                }
            }

            // Clone page Resources dict carefully
            PdfDict mergedResDict;
            if (const PdfValue* resVal = srcDoc->Resolve(srcPageVal.DictGet("Resources"))) {
                if (resVal->IsDict()) {
                    PdfValue rewritten = builder.RewriteValueRefs(*srcDoc, *resVal, remap, 1);
                    if (rewritten.IsDict()) mergedResDict = std::move(rewritten.dictVal);
                }
            }

            // If there are annotations or non-plain paper overlay, wrap old contents in q/Q and append overlay stream
            std::map<std::string, std::uint32_t> overlayImages;
            if (!pg.objects.empty()) {
                std::ostringstream overlay;
                overlay << "q\n";
                overlay << BuildPageOverlayCtm(finalCrop.x, finalCrop.y, finalCrop.Right(), finalCrop.Bottom(), pg.rotationDeg);
                for (const auto& obj : pg.objects) {
                    EmitAnnotationObjectOps(overlay, obj, doc, builder, overlayImages, 0.0, 0.0, true);
                }
                overlay << "Q\n";

                const std::string ovStr = overlay.str();
                const std::uint32_t wrapOpen = builder.AddStream("", { 'q', '\n' }, false);
                const std::uint32_t wrapClose = builder.AddStream("", { 'Q', '\n' }, false);
                const std::uint32_t ovId = builder.AddStream("", std::vector<std::uint8_t>(ovStr.begin(), ovStr.end()), true);

                std::vector<std::uint32_t> finalContents;
                if (!clonedContentIds.empty()) {
                    finalContents.push_back(wrapOpen);
                    finalContents.insert(finalContents.end(), clonedContentIds.begin(), clonedContentIds.end());
                    finalContents.push_back(wrapClose);
                }
                finalContents.push_back(ovId);
                clonedContentIds = std::move(finalContents);

                // Ensure overlay fonts, ExtGState, and XObjects exist cleanly in mergedResDict without clobbering existing F1/F2
                // Use unique resource names /FDocR1 /FDocR2 /GSDocAlpha and patch content stream or check collisions
                // Actually check: we used /F1 /F2 /GSAlpha in EmitAnnotationObjectOps - let's add them if missing, or rename cleanly!
                auto ensureSubDict = [&](const std::string& name) -> PdfDict& {
                    auto it = mergedResDict.find(name);
                    if (it == mergedResDict.end() || !it->second.IsDict()) {
                        PdfValue dv;
                        dv.kind = PdfValue::Kind::Dict;
                        mergedResDict[name] = std::move(dv);
                    }
                    return mergedResDict[name].dictVal;
                };
                auto makeRef = [](std::uint32_t id) {
                    PdfValue rv;
                    rv.kind = PdfValue::Kind::Ref;
                    rv.refVal = { id, 0 };
                    return rv;
                };
                PdfDict& fd = ensureSubDict("Font");
                if (fd.find("F1") == fd.end()) fd["F1"] = makeRef(f1);
                if (fd.find("F2") == fd.end()) fd["F2"] = makeRef(f2);
                PdfDict& gd = ensureSubDict("ExtGState");
                if (gd.find("GSAlpha") == gd.end()) gd["GSAlpha"] = makeRef(gsAlpha);
                if (!overlayImages.empty()) {
                    PdfDict& xd = ensureSubDict("XObject");
                    for (const auto& ikv : overlayImages) {
                        xd[ikv.first] = makeRef(ikv.second);
                    }
                }
            }

            const std::uint32_t pageId = builder.AllocId();
            std::ostringstream pgBody;
            pgBody << "<</Type /Page /Parent " << pagesRootId << " 0 R "
                   << "/MediaBox [" << FmtNum(srcPageInfo.mediaBox.x) << ' ' << FmtNum(srcPageInfo.mediaBox.y) << ' '
                   << FmtNum(srcPageInfo.mediaBox.Right()) << ' ' << FmtNum(srcPageInfo.mediaBox.Bottom()) << "] "
                   << "/CropBox [" << FmtNum(finalCrop.x) << ' ' << FmtNum(finalCrop.y) << ' '
                   << FmtNum(finalCrop.Right()) << ' ' << FmtNum(finalCrop.Bottom()) << "] "
                   << "/Rotate " << (((pg.rotationDeg % 360) + 360) % 360) << " ";
            if (!mergedResDict.empty()) {
                PdfValue rv;
                rv.kind = PdfValue::Kind::Dict;
                rv.dictVal = std::move(mergedResDict);
                pgBody << "/Resources " << PdfDocument::SerializeValue(rv) << ' ';
            }
            if (!clonedContentIds.empty()) {
                pgBody << "/Contents [";
                for (std::size_t i = 0; i < clonedContentIds.size(); ++i) {
                    if (i > 0) pgBody << ' ';
                    pgBody << clonedContentIds[i] << " 0 R";
                }
                pgBody << "] ";
            }
            pgBody << ">>";
            builder.SetObject(pageId, pgBody.str());
            pageIds.push_back(pageId);
        } else {
            // Standalone note/blank page with paper pattern + vector annotations
            const double w = (std::max)(36.0, pg.widthPt * (pg.crop.right - pg.crop.left));
            const double h = (std::max)(36.0, pg.heightPt * (pg.crop.bottom - pg.crop.top));
            std::map<std::string, std::uint32_t> pageImages;
            std::ostringstream stream;
            EmitPaperPatternOps(stream, w, h, pg.background);
            if (!pg.objects.empty()) {
                stream << "q 1 0 0 -1 0 " << FmtNum(h) << " cm\n";
                for (const auto& obj : pg.objects) {
                    EmitAnnotationObjectOps(stream, obj, doc, builder, pageImages,
                        -pg.widthPt * pg.crop.left, -pg.heightPt * pg.crop.top, true);
                }
                stream << "Q\n";
            }
            const std::string sBytes = stream.str();
            const std::uint32_t cid = builder.AddStream("", std::vector<std::uint8_t>(sBytes.begin(), sBytes.end()), true);

            const std::uint32_t pageId = builder.AllocId();
            std::ostringstream pgBody;
            pgBody << "<</Type /Page /Parent " << pagesRootId << " 0 R "
                   << "/MediaBox [0 0 " << FmtNum(w) << ' ' << FmtNum(h) << "] "
                   << "/Rotate " << (((pg.rotationDeg % 360) + 360) % 360) << " "
                   << "/Resources <</Font <</F1 " << f1 << " 0 R /F2 " << f2 << " 0 R>> "
                   << "/ExtGState <</GSAlpha " << gsAlpha << " 0 R>>";
            if (!pageImages.empty()) {
                pgBody << " /XObject <<";
                for (const auto& ikv : pageImages) {
                    pgBody << '/' << ikv.first << ' ' << ikv.second << " 0 R ";
                }
                pgBody << ">>";
            }
            pgBody << ">> /Contents " << cid << " 0 R>>";
            builder.SetObject(pageId, pgBody.str());
            pageIds.push_back(pageId);
        }
    }

    std::ostringstream pagesDict;
    pagesDict << "<</Type /Pages /Count " << pageIds.size() << " /Kids [";
    for (std::size_t i = 0; i < pageIds.size(); ++i) {
        if (i > 0) pagesDict << ' ';
        pagesDict << pageIds[i] << " 0 R";
    }
    pagesDict << "]>>";
    builder.SetObject(pagesRootId, pagesDict.str());

    const std::uint32_t catalogId = builder.AddObject("<</Type /Catalog /Pages " + std::to_string(pagesRootId) + " 0 R>>");
    outPdfBytes = builder.Finalize(catalogId);
    return true;
}

bool PdfWriter::ExportDocumentToFile(
    const DocumentModel& doc,
    const std::wstring& outPath,
    std::string* outErr)
{
    std::vector<std::uint8_t> bytes;
    if (!ExportDocumentToBytes(doc, bytes, outErr)) return false;
    return WriteAllBytesToFile(outPath, bytes, outErr);
}

bool PdfWriter::ExportInfiniteCanvasToBytes(
    const DocumentModel& doc,
    const CanvasPdfExportOptions& opts,
    std::vector<std::uint8_t>& outPdfBytes,
    std::string* outErr)
{
    DocRect region{};
    if (opts.hasCustomRegion && !opts.customRegionWorld.Empty()) {
        region = opts.customRegionWorld;
    } else if (!doc.CanvasObjects().empty()) {
        bool first = true;
        double minX = 0, minY = 0, maxX = 0, maxY = 0;
        for (const auto& obj : doc.CanvasObjects()) {
            if (first) {
                minX = obj.bounds.x;
                minY = obj.bounds.y;
                maxX = obj.bounds.Right();
                maxY = obj.bounds.Bottom();
                first = false;
            } else {
                minX = (std::min)(minX, obj.bounds.x);
                minY = (std::min)(minY, obj.bounds.y);
                maxX = (std::max)(maxX, obj.bounds.Right());
                maxY = (std::max)(maxY, obj.bounds.Bottom());
            }
        }
        region = { minX - 24.0, minY - 24.0, (std::max)(200.0, (maxX - minX) + 48.0), (std::max)(200.0, (maxY - minY) + 48.0) };
    } else {
        region = { 0.0, 0.0, opts.pageWidthPt, opts.pageHeightPt };
    }

    PdfBuilder builder;
    const std::uint32_t f1 = builder.AddStandardHelveticaFont(false);
    const std::uint32_t f2 = builder.AddStandardHelveticaFont(true);
    const std::uint32_t gsAlpha = builder.AddHighlightExtGState(0.38);
    const std::uint32_t pagesRootId = builder.AllocId();
    std::vector<std::uint32_t> pageIds;

    auto emitSlicePage = [&](const DocRect& sliceRect) {
        const double pw = (std::max)(72.0, sliceRect.w);
        const double ph = (std::max)(72.0, sliceRect.h);
        std::map<std::string, std::uint32_t> imgs;
        std::ostringstream ss;
        EmitPaperPatternOps(ss, pw, ph, doc.CanvasBackground());
        ss << "q 1 0 0 -1 0 " << FmtNum(ph) << " cm\n";

        const std::vector<std::uint64_t> ids = doc.QueryCanvasRect(sliceRect);
        std::vector<const AnnotationObject*> visible;
        visible.reserve(ids.size());
        for (std::uint64_t id : ids) {
            if (const auto* o = doc.FindCanvasObject(id)) visible.push_back(o);
        }
        std::sort(visible.begin(), visible.end(), [](const AnnotationObject* a, const AnnotationObject* b) {
            return a->zOrder < b->zOrder;
        });

        for (const AnnotationObject* o : visible) {
            if (o->kind == ObjectKind::PdfCard) {
                // Draw PdfCard placeholder frame + label
                ss << "0.88 0.90 0.94 rg 0.25 0.30 0.40 RG 1 w "
                   << FmtNum(o->bounds.x - sliceRect.x) << ' ' << FmtNum(o->bounds.y - sliceRect.y)
                   << ' ' << FmtNum(o->bounds.w) << ' ' << FmtNum(o->bounds.h) << " re B\n";
                ss << "0.1 0.1 0.15 rg BT /F2 11 Tf 1 0 0 -1 "
                   << FmtNum(o->bounds.x - sliceRect.x + 10.0) << ' ' << FmtNum(o->bounds.y - sliceRect.y + 20.0)
                   << " Tm " << EscapePdfLiteralString(Utf8ToWinAnsi("PDF p." + std::to_string(o->pdfSourcePageIndex0 + 1))) << " Tj ET\n";
            } else {
                EmitAnnotationObjectOps(ss, *o, doc, builder, imgs, -sliceRect.x, -sliceRect.y, true);
            }
        }
        ss << "Q\n";

        const std::string sStr = ss.str();
        const std::uint32_t cid = builder.AddStream("", std::vector<std::uint8_t>(sStr.begin(), sStr.end()), true);
        const std::uint32_t pageId = builder.AllocId();
        std::ostringstream pg;
        pg << "<</Type /Page /Parent " << pagesRootId << " 0 R /MediaBox [0 0 " << FmtNum(pw) << ' ' << FmtNum(ph) << "] "
           << "/Resources <</Font <</F1 " << f1 << " 0 R /F2 " << f2 << " 0 R>> /ExtGState <</GSAlpha " << gsAlpha << " 0 R>>";
        if (!imgs.empty()) {
            pg << " /XObject <<";
            for (const auto& ikv : imgs) pg << '/' << ikv.first << ' ' << ikv.second << " 0 R ";
            pg << ">>";
        }
        pg << ">> /Contents " << cid << " 0 R>>";
        builder.SetObject(pageId, pg.str());
        pageIds.push_back(pageId);
    };

    if (!opts.paginated) {
        emitSlicePage(region);
    } else {
        const double pw = (std::max)(144.0, opts.pageWidthPt);
        const double ph = (std::max)(144.0, opts.pageHeightPt);
        const int cols = (std::max)(1, static_cast<int>(std::ceil(region.w / pw)));
        const int rows = (std::max)(1, static_cast<int>(std::ceil(region.h / ph)));
        for (int r = 0; r < rows && pageIds.size() < 200; ++r) {
            for (int c = 0; c < cols && pageIds.size() < 200; ++c) {
                const DocRect tile{ region.x + c * pw, region.y + r * ph, pw, ph };
                if (r == 0 && c == 0 || !doc.QueryCanvasRect(tile).empty()) {
                    emitSlicePage(tile);
                }
            }
        }
    }

    if (pageIds.empty()) {
        if (outErr) *outErr = "Sin contenido exportable en lienzo infinito.";
        return false;
    }

    std::ostringstream pagesDict;
    pagesDict << "<</Type /Pages /Count " << pageIds.size() << " /Kids [";
    for (std::size_t i = 0; i < pageIds.size(); ++i) {
        if (i > 0) pagesDict << ' ';
        pagesDict << pageIds[i] << " 0 R";
    }
    pagesDict << "]>>";
    builder.SetObject(pagesRootId, pagesDict.str());
    const std::uint32_t catalogId = builder.AddObject("<</Type /Catalog /Pages " + std::to_string(pagesRootId) + " 0 R>>");
    outPdfBytes = builder.Finalize(catalogId);
    return true;
}

bool PdfWriter::ExportInfiniteCanvasToFile(
    const DocumentModel& doc,
    const CanvasPdfExportOptions& opts,
    const std::wstring& outPath,
    std::string* outErr)
{
    std::vector<std::uint8_t> bytes;
    if (!ExportInfiniteCanvasToBytes(doc, opts, bytes, outErr)) return false;
    return WriteAllBytesToFile(outPath, bytes, outErr);
}

bool PdfWriter::MergePdfFiles(
    const std::vector<std::wstring>& inputPaths,
    const std::wstring& outPath,
    std::string* outErr)
{
    if (inputPaths.empty()) {
        if (outErr) *outErr = "No se especificaron archivos PDF para combinar.";
        return false;
    }
    DocumentModel merged;
    merged.ResetToBlank(DocumentMode::PaginatedPdf);
    merged.MutablePages().clear();

    for (const auto& p : inputPaths) {
        PdfDocument pdf;
        if (!pdf.LoadFromFile(p, outErr)) return false;
        const std::string pUtf8 = WideToUtf8(p);
        for (const auto& pi : pdf.Pages()) {
            PageModel pm;
            pm.pageId = merged.AllocatePageId();
            const DocRect box = pi.hasCropBox ? pi.cropBox : pi.mediaBox;
            pm.widthPt = (std::max)(36.0, box.w);
            pm.heightPt = (std::max)(36.0, box.h);
            pm.rotationDeg = pi.rotationDeg;
            pm.hasPdfSource = true;
            pm.sourcePdfPathUtf8 = pUtf8;
            pm.sourcePageIndex0 = static_cast<int>(pi.pageIndex0);
            pm.extractedTextUtf8 = pi.extractedTextUtf8;
            merged.MutablePages().push_back(std::move(pm));
        }
    }
    merged.RebuildLayout();
    return ExportDocumentToFile(merged, outPath, outErr);
}

bool PdfWriter::SplitPdfFile(
    const std::wstring& inputPath,
    const std::vector<std::size_t>& zeroBasedPages,
    const std::wstring& outPath,
    std::string* outErr)
{
    PdfDocument pdf;
    if (!pdf.LoadFromFile(inputPath, outErr)) return false;

    DocumentModel subset;
    subset.ResetToBlank(DocumentMode::PaginatedPdf);
    subset.MutablePages().clear();
    const std::string pUtf8 = WideToUtf8(inputPath);

    for (std::size_t idx : zeroBasedPages) {
        if (idx >= pdf.PageCount()) continue;
        const auto& pi = pdf.PageAt(idx);
        PageModel pm;
        pm.pageId = subset.AllocatePageId();
        const DocRect box = pi.hasCropBox ? pi.cropBox : pi.mediaBox;
        pm.widthPt = (std::max)(36.0, box.w);
        pm.heightPt = (std::max)(36.0, box.h);
        pm.rotationDeg = pi.rotationDeg;
        pm.hasPdfSource = true;
        pm.sourcePdfPathUtf8 = pUtf8;
        pm.sourcePageIndex0 = static_cast<int>(pi.pageIndex0);
        pm.extractedTextUtf8 = pi.extractedTextUtf8;
        subset.MutablePages().push_back(std::move(pm));
    }
    if (subset.Pages().empty()) {
        if (outErr) *outErr = "Ninguna p\xC3\xA1gina v\xC3\xA1lida seleccionada para dividir.";
        return false;
    }
    subset.RebuildLayout();
    return ExportDocumentToFile(subset, outPath, outErr);
}

bool PdfWriter::TryDirectReplaceTextInPdfBytes(
    const std::vector<std::uint8_t>& srcPdfBytes,
    std::size_t pageIndex0,
    const std::string& oldTextUtf8,
    const std::string& newTextUtf8,
    std::vector<std::uint8_t>& outPdfBytes,
    std::string* outErr)
{
    PdfDocument doc;
    if (!doc.LoadFromBytes(srcPdfBytes, outErr)) return false;
    const auto cap = doc.InspectTextReplacement(pageIndex0, oldTextUtf8);
    if (!cap.supportedDirectStream) {
        if (outErr) *outErr = cap.diagnosticUtf8;
        return false;
    }

    const std::string oldLit = EscapePdfLiteralString(Utf8ToWinAnsi(oldTextUtf8));
    const std::string newLit = EscapePdfLiteralString(Utf8ToWinAnsi(newTextUtf8));

    // Rewrite PDF cleanly replacing matching page's decompressed content stream with updated stream
    PdfBuilder builder;
    std::unordered_map<std::uint32_t, std::uint32_t> remap;
    const std::uint32_t pagesRootId = builder.AllocId();
    std::vector<std::uint32_t> pageIds;

    for (std::size_t i = 0; i < doc.PageCount(); ++i) {
        const auto& pi = doc.PageAt(i);
        const PdfValue pageVal = doc.LookupObject(pi.pageRef.obj);
        PdfValue rewrittenPage = builder.RewriteValueRefs(doc, pageVal, remap, 1);
        PdfDict d = rewrittenPage.dictVal;

        if (i == pageIndex0) {
            std::vector<std::uint8_t> raw = doc.GetPageContentBytes(i);
            std::string txt(reinterpret_cast<const char*>(raw.data()), raw.size());
            std::size_t pos = 0;
            std::size_t replaced = 0;
            while ((pos = txt.find(oldLit, pos)) != std::string::npos) {
                txt.replace(pos, oldLit.size(), newLit);
                pos += newLit.size();
                ++replaced;
            }
            if (replaced == 0) {
                if (outErr) *outErr = "Cadena literal no reemplazada en stream.";
                return false;
            }
            const std::uint32_t newStreamId = builder.AddStream("", std::vector<std::uint8_t>(txt.begin(), txt.end()), true);
            PdfValue cref;
            cref.kind = PdfValue::Kind::Ref;
            cref.refVal = { newStreamId, 0 };
            d["Contents"] = cref;
        }

        PdfValue parentRef;
        parentRef.kind = PdfValue::Kind::Ref;
        parentRef.refVal = { pagesRootId, 0 };
        d["Parent"] = parentRef;

        PdfValue dictV;
        dictV.kind = PdfValue::Kind::Dict;
        dictV.dictVal = std::move(d);
        pageIds.push_back(builder.AddObject(PdfDocument::SerializeValue(dictV)));
    }

    std::ostringstream pagesDict;
    pagesDict << "<</Type /Pages /Count " << pageIds.size() << " /Kids [";
    for (std::size_t i = 0; i < pageIds.size(); ++i) {
        if (i > 0) pagesDict << ' ';
        pagesDict << pageIds[i] << " 0 R";
    }
    pagesDict << "]>>";
    builder.SetObject(pagesRootId, pagesDict.str());
    const std::uint32_t catId = builder.AddObject("<</Type /Catalog /Pages " + std::to_string(pagesRootId) + " 0 R>>");
    outPdfBytes = builder.Finalize(catId);
    return true;
}

} // namespace DocStudio
