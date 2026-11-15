#pragma once
#include <string>
#include <vector>
#include <cmath>
#include <sstream>
#include <iomanip>
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#ifndef M_E
#define M_E  2.71828182845904523536
#endif

enum AngleMode { ANG_DEG = 0, ANG_RAD = 1, ANG_GRA = 2 };

struct CalcState {
    std::wstring expression;
    std::wstring resultStr;
    double       lastAns = 0.0;
    AngleMode    angleMode = ANG_DEG;
    bool         isShift = false;
    bool         isAlpha = false;
    bool         hasEvaluated = false;
    int          cursorPos = 0;
    
    // Variables A..F, X, Y, M
    double vars[9] = {0}; 

    void Reset() {
        expression.clear();
        resultStr.clear();
        hasEvaluated = false;
        isShift = false;
        isAlpha = false;
        cursorPos = 0;
    }
};

// ── Recursive descent scientific math evaluator ──────────────────────────────
class MathEvaluator {
public:
    static bool Evaluate(const std::wstring& expr, double ans, AngleMode mode, double& outVal, std::wstring& outErr) {
        if (expr.empty()) {
            outVal = 0.0;
            return true;
        }
        std::wstring s = Preprocess(expr, ans);
        size_t pos = 0;
        try {
            outVal = ParseExpression(s, pos, mode);
            if (pos < s.length()) {
                outErr = L"Syntax ERROR";
                return false;
            }
            if (std::isnan(outVal) || std::isinf(outVal)) {
                outErr = L"Math ERROR";
                return false;
            }
            return true;
        } catch (...) {
            outErr = L"Syntax ERROR";
            return false;
        }
    }

    static std::wstring FormatResult(double val) {
        if (std::isnan(val) || std::isinf(val)) return L"Math ERROR";
        if (std::abs(val) < 1e-12 && std::abs(val) > -1e-12) return L"0";

        // Check if integer
        if (std::abs(val - std::round(val)) < 1e-10 && std::abs(val) < 1e12) {
            return std::to_wstring((long long)std::round(val));
        }

        std::wostringstream oss;
        oss << std::setprecision(10) << val;
        std::wstring str = oss.str();
        return str;
    }

private:
    static std::wstring Preprocess(const std::wstring& expr, double ans) {
        std::wstring s;
        for (size_t i = 0; i < expr.length(); ++i) {
            wchar_t c = expr[i];
            if (c == L' ') continue;
            if (c == L'x' || c == L'\x00D7') s += L'*';
            else if (c == L'\x00F7') s += L'/';
            else if (c == L'\x2212') s += L'-';
            else if (c == L'\x03C0') s += L"3.141592653589793";
            else if (expr.substr(i, 3) == L"Ans") {
                s += L"(" + std::to_wstring(ans) + L")";
                i += 2;
            }
            else s += c;
        }
        return s;
    }

    static double ParseExpression(const std::wstring& s, size_t& pos, AngleMode mode) {
        double val = ParseTerm(s, pos, mode);
        while (pos < s.length()) {
            if (s[pos] == L'+') { pos++; val += ParseTerm(s, pos, mode); }
            else if (s[pos] == L'-') { pos++; val -= ParseTerm(s, pos, mode); }
            else break;
        }
        return val;
    }

    static double ParseTerm(const std::wstring& s, size_t& pos, AngleMode mode) {
        double val = ParseFactor(s, pos, mode);
        while (pos < s.length()) {
            if (s[pos] == L'*') { pos++; val *= ParseFactor(s, pos, mode); }
            else if (s[pos] == L'/') {
                pos++;
                double denom = ParseFactor(s, pos, mode);
                if (std::abs(denom) < 1e-15) throw 1;
                val /= denom;
            }
            else break;
        }
        return val;
    }

    static double ParseFactor(const std::wstring& s, size_t& pos, AngleMode mode) {
        double val = ParsePrimary(s, pos, mode);
        while (pos < s.length()) {
            if (s[pos] == L'^') {
                pos++;
                double exponent = ParsePrimary(s, pos, mode);
                val = std::pow(val, exponent);
            } else if (s[pos] == L'!') {
                pos++;
                val = Factorial(val);
            } else break;
        }
        return val;
    }

    static double ParsePrimary(const std::wstring& s, size_t& pos, AngleMode mode) {
        if (pos >= s.length()) throw 1;

        // Unary minus/plus
        if (s[pos] == L'-') { pos++; return -ParsePrimary(s, pos, mode); }
        if (s[pos] == L'+') { pos++; return ParsePrimary(s, pos, mode); }

        // Parentheses
        if (s[pos] == L'(') {
            pos++;
            double val = ParseExpression(s, pos, mode);
            if (pos < s.length() && s[pos] == L')') pos++;
            return val;
        }

        // Functions
        static const struct { const wchar_t* name; int len; } funcs[] = {
            { L"sin", 3 }, { L"cos", 3 }, { L"tan", 3 },
            { L"asin", 4 }, { L"acos", 4 }, { L"atan", 4 },
            { L"sqrt", 4 }, { L"ln", 2 }, { L"log", 3 }, { L"abs", 3 }
        };

        for (const auto& f : funcs) {
            if (s.substr(pos, f.len) == f.name) {
                pos += f.len;
                double arg = ParsePrimary(s, pos, mode);
                return ApplyFunc(f.name, arg, mode);
            }
        }

        // Numbers
        size_t start = pos;
        bool hasDot = false;
        while (pos < s.length() && ((s[pos] >= L'0' && s[pos] <= L'9') || (s[pos] == L'.' && !hasDot))) {
            if (s[pos] == L'.') hasDot = true;
            pos++;
        }

        if (start == pos) throw 1;
        return std::stod(s.substr(start, pos - start));
    }

    static double ApplyFunc(const std::wstring& name, double arg, AngleMode mode) {
        if (name == L"sqrt") {
            if (arg < 0) throw 1;
            return std::sqrt(arg);
        }
        if (name == L"ln") {
            if (arg <= 0) throw 1;
            return std::log(arg);
        }
        if (name == L"log") {
            if (arg <= 0) throw 1;
            return std::log10(arg);
        }
        if (name == L"abs") return std::abs(arg);

        double rad = arg;
        if (mode == ANG_DEG) rad = arg * M_PI / 180.0;
        else if (mode == ANG_GRA) rad = arg * M_PI / 200.0;

        if (name == L"sin") return std::sin(rad);
        if (name == L"cos") return std::cos(rad);
        if (name == L"tan") {
            if (std::abs(std::cos(rad)) < 1e-14) throw 1;
            return std::tan(rad);
        }
        if (name == L"asin") {
            if (arg < -1.0 || arg > 1.0) throw 1;
            double r = std::asin(arg);
            return ToAngle(r, mode);
        }
        if (name == L"acos") {
            if (arg < -1.0 || arg > 1.0) throw 1;
            double r = std::acos(arg);
            return ToAngle(r, mode);
        }
        if (name == L"atan") {
            double r = std::atan(arg);
            return ToAngle(r, mode);
        }
        return arg;
    }

    static double ToAngle(double rad, AngleMode mode) {
        if (mode == ANG_DEG) return rad * 180.0 / M_PI;
        if (mode == ANG_GRA) return rad * 200.0 / M_PI;
        return rad;
    }

    static double Factorial(double val) {
        if (val < 0 || val > 170 || std::floor(val) != val) throw 1;
        double res = 1.0;
        for (int i = 2; i <= (int)val; ++i) res *= i;
        return res;
    }
};
