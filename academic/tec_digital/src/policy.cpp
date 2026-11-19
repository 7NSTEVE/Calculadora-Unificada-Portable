#include "academic/tec_digital/policy.hpp"
#include "http_common.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <regex>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace academic::tec_digital {

namespace {

int HexVal(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    return -1;
}

std::string UrlDecode(std::string_view sv) {
    std::string out;
    out.reserve(sv.size());
    for (size_t i = 0; i < sv.size(); ++i) {
        if (sv[i] == '+') {
            out.push_back(' ');
        } else if (sv[i] == '%' && i + 2 < sv.size()) {
            int hi = HexVal(sv[i + 1]);
            int lo = HexVal(sv[i + 2]);
            if (hi >= 0 && lo >= 0) {
                out.push_back(static_cast<char>((hi << 4) | lo));
                i += 2;
                continue;
            }
            out.push_back(sv[i]);
        } else {
            out.push_back(sv[i]);
        }
    }
    return out;
}

std::vector<std::pair<std::string, std::string>> ParseQueryPairs(std::string_view query) {
    std::vector<std::pair<std::string, std::string>> out;
    if (query.empty()) return out;
    size_t pos = 0;
    while (pos <= query.size()) {
        size_t amp = query.find('&', pos);
        if (amp == std::string_view::npos) amp = query.size();
        std::string_view item(query.data() + pos, amp - pos);
        if (!item.empty()) {
            size_t eq = item.find('=');
            std::string k = UrlDecode(eq == std::string_view::npos ? item : item.substr(0, eq));
            std::string v = (eq == std::string_view::npos) ? "" : UrlDecode(item.substr(eq + 1));
            out.emplace_back(std::move(k), std::move(v));
        }
        if (amp == query.size()) break;
        pos = amp + 1;
    }
    return out;
}

bool ContainsCI(std::string_view haystack, std::string_view needle) {
    return AsciiLower(haystack).find(AsciiLower(needle)) != std::string::npos;
}

bool IsPassiveResourceType(std::string_view resource_type) {
    std::string r = AsciiLower(resource_type);
    return r == "script" || r == "stylesheet" || r == "image" || r == "font" || r == "media";
}

bool DecodeAndNormalizePolicyPath(std::string_view raw_path, std::string& out_path) {
    std::string decoded;
    decoded.reserve(raw_path.size());
    for (size_t i = 0; i < raw_path.size(); ++i) {
        char c = raw_path[i];
        if (c == '\\') {
            return false;
        }
        if (c == '%') {
            if (i + 2 >= raw_path.size()) return false;
            int hi = HexVal(raw_path[i + 1]);
            int lo = HexVal(raw_path[i + 2]);
            if (hi < 0 || lo < 0) return false;
            unsigned char b = static_cast<unsigned char>((hi << 4) | lo);
            if (b == 0x00 || b == '/' || b == '\\') return false;
            decoded.push_back(static_cast<char>(b));
            i += 2;
        } else {
            decoded.push_back(c);
        }
    }

    std::vector<std::string_view> stack;
    const bool had_trailing_slash = !decoded.empty() && decoded.back() == '/';
    bool ended_with_dot = false;
    size_t i = 0;
    while (i < decoded.size()) {
        while (i < decoded.size() && decoded[i] == '/') ++i;
        if (i >= decoded.size()) break;
        size_t j = decoded.find('/', i);
        if (j == std::string::npos) j = decoded.size();
        std::string_view seg(decoded.data() + i, j - i);
        ended_with_dot = (seg == "." || seg == "..");
        if (seg == ".") {
            // skip
        } else if (seg == "..") {
            if (!stack.empty()) stack.pop_back();
        } else {
            stack.push_back(seg);
        }
        i = j;
    }

    out_path = "/";
    for (size_t idx = 0; idx < stack.size(); ++idx) {
        if (idx > 0) out_path.push_back('/');
        out_path.append(stack[idx]);
    }
    if ((had_trailing_slash || ended_with_dot) && out_path.size() > 1) {
        out_path.push_back('/');
    }
    return true;
}

// Strip final filename segment ONLY when the already-decoded, dot-normalized path
// genuinely matches /file-storage/(view|download)/[^/]+$ so filenames like
// "tarea_delete.pdf" do not trigger false-positive path-segment blocks.
std::string PathForSegmentCheck(std::string_view norm_path) {
    static const std::regex kGenuineFileStorageLeafRe(
        R"(^.*/file-storage/(?:view|download)/[^/]+$)",
        std::regex::ECMAScript | std::regex::icase);
    std::string path_str(norm_path);
    if (std::regex_match(path_str, kGenuineFileStorageLeafRe)) {
        size_t last_slash = path_str.rfind('/');
        if (last_slash != std::string::npos && last_slash + 1 < path_str.size()) {
            return path_str.substr(0, last_slash + 1);
        }
    }
    return path_str;
}

bool IsMutatingPathOrQuery(const ParsedUrl& parsed) {
    const std::string check_path = PathForSegmentCheck(parsed.path);
    const std::string full_low_path = AsciiLower(parsed.path);
    const std::string full_low_target =
        AsciiLower(parsed.path + (parsed.query.empty() ? "" : "?" + parsed.query));

    // Read-only false-positive exclusions before broad pattern checks:
    // - forums/message-view?message_id=...
    // - /evaluation/tda-ce-estudiante/tda-index or evaluation/view
    static const std::regex kMutatingSegmentRe(
        R"((?:^|[/_.-])(?:delete|remove|edit|update|save|submit|publish|upload|move|rename|enroll|deregister|unregister|withdraw|unsubscribe|create|add|approve|reject|grade|send|reply|logout|log[-_]?out|sign[-_]?out|mark[-_]?(?:all[-_]?)?read|borrar|eliminar|editar|actualizar|guardar|subir|mover|renombrar|inscribir|desinscribir|aprobar|rechazar|calificar|responder)(?:$|[/_.-]|\d))",
        std::regex::ECMAScript | std::regex::icase);

    if (std::regex_search(check_path, kMutatingSegmentRe)) {
        return true;
    }

    static const std::regex kExactSegmentBlockRe(
        R"((?:^|/)(?:log_registration|tam-tracking|to-tam-tracking|matricula)(?:/|$))",
        std::regex::ECMAScript | std::regex::icase);

    if (std::regex_search(check_path, kExactSegmentBlockRe)) {
        return true;
    }

    static const std::regex kUrlBlockRe(
        R"((/register/logout|/logout\b|/log-out\b|unsubscribe|deregister|configure-element|op=shade|op=unshade|op=delete|op=remove|op=drop|op=subscribe|op=hide|op=show|op=move|op=config|/admin/|administer|membership-toggle|toggle-membership|/spam/|comment-delete|file-delete|folder-delete|news-delete|member-confirm(?:-\d+)?|/member-delete|/member-remove|/cal-item-new\b|request-change-frequency|password-update|cerrar[-_ ]?sesi))",
        std::regex::ECMAScript | std::regex::icase);

    if (std::regex_search(full_low_target, kUrlBlockRe)) {
        return true;
    }

    static const std::regex kActionKeyRe(R"(^(?:action|operation|cmd|mode|op)$)",
                                         std::regex::ECMAScript | std::regex::icase);
    static const std::regex kMutatingOpRe(
        R"(^(?:delete|remove|update|save|submit|publish|upload|move|rename|enroll|deregister|unregister|withdraw|unsubscribe|create|add|approve|reject|grade|send|reply|mark_read|shade|unshade|hide|show|drop|subscribe|config|borrar|eliminar)$)",
        std::regex::ECMAScript | std::regex::icase);

    auto qpairs = ParseQueryPairs(parsed.query);
    for (const auto& kv : qpairs) {
        if (std::regex_match(kv.first, kActionKeyRe) &&
            std::regex_match(kv.second, kMutatingOpRe)) {
            return true;
        }
    }

    if (ContainsCI(full_low_path, "/evaluation") || ContainsCI(full_low_path, "/assessment")) {
        static const std::regex kEvalMutateRe(
            R"((?:^|[/_.-])(?:submit|upload|answer|respond|delete|confirm|calificar|entregar|responder|grade-change|qualify)(?:$|[/_.-]))",
            std::regex::ECMAScript | std::regex::icase);
        if (std::regex_search(full_low_path, kEvalMutateRe)) {
            return true;
        }
    }

    return false;
}

}  // namespace

ReadOnlyRequestPolicy::ReadOnlyRequestPolicy(PolicyOptions opts)
    : opts_(std::move(opts)) {
    detail::WarmupRegexLocale();
}

bool ReadOnlyRequestPolicy::IsTecDigitalOrigin(const ParsedUrl& parsed) const {
    if (!parsed.valid) return false;
    if (parsed.scheme == "https" && parsed.host == "tecdigital.tec.ac.cr" && parsed.port == 443) {
        return true;
    }
    for (const auto& test_origin : opts_.allowed_test_origins) {
        ParsedUrl to = ParseHttpUrl(test_origin);
        if (to.valid && IsLoopbackHost(to.host) && parsed.origin == to.origin) {
            return true;
        }
    }
    return false;
}

bool ReadOnlyRequestPolicy::IsGraphQlEndpoint(const ParsedUrl& parsed) const {
    if (!parsed.valid) return false;
    if (parsed.scheme == "https" && parsed.host == "api.tec.ac.cr" &&
        parsed.port == 3005 && parsed.path == "/graphql") {
        return true;
    }
    for (const auto& test_origin : opts_.allowed_test_origins) {
        ParsedUrl to = ParseHttpUrl(test_origin);
        if (to.valid && IsLoopbackHost(to.host) &&
            parsed.origin == to.origin && parsed.path == "/graphql") {
            return true;
        }
    }
    return false;
}

bool ReadOnlyRequestPolicy::IsAllowedOrigin(std::string_view url) const {
    ParsedUrl parsed = ParseHttpUrl(url);
    if (!parsed.valid) return false;
    std::string norm_path;
    if (!DecodeAndNormalizePolicyPath(parsed.path, norm_path)) {
        return false;
    }
    parsed.path = std::move(norm_path);
    return IsTecDigitalOrigin(parsed) || IsGraphQlEndpoint(parsed);
}

bool ReadOnlyRequestPolicy::IsReadOnlyGraphQL(std::string_view query) {
    detail::WarmupRegexLocale();
    // Strip #... comments to EOL
    std::string doc;
    doc.reserve(query.size());
    bool in_comment = false;
    for (char c : query) {
        if (in_comment) {
            if (c == '\r' || c == '\n') {
                in_comment = false;
                doc.push_back(' ');
            }
        } else if (c == '#') {
            in_comment = true;
            doc.push_back(' ');
        } else {
            doc.push_back(c);
        }
    }
    doc = TrimAscii(doc);
    if (doc.empty()) return false;

    static const std::regex kStartsWithQueryOrBrace(R"(^(?:query\b|\{))", std::regex::ECMAScript);
    if (!std::regex_search(doc, kStartsWithQueryOrBrace)) {
        return false;
    }

    static const std::regex kHasMutationOrSubscription(
        R"(\b(?:mutation|subscription)\b)",
        std::regex::ECMAScript | std::regex::icase);
    if (std::regex_search(doc, kHasMutationOrSubscription)) {
        return false;
    }
    return true;
}

bool ReadOnlyRequestPolicy::IsPrematriculaReadOnly(std::string_view body) {
    std::string trimmed = TrimAscii(body);
    if (trimmed.empty()) return false;

    static const std::unordered_set<std::string> kPermittedKeys = {
        "tipo", "pre_enrollment_id", "curriculum_id", "curriculum_id_current"
    };

    if (trimmed.front() == '{') {
        try {
            auto j = nlohmann::json::parse(trimmed);
            if (!j.is_object() || j.empty()) return false;
            std::string tipo_val;
            bool has_tipo = false;
            for (auto it = j.begin(); it != j.end(); ++it) {
                if (kPermittedKeys.count(it.key()) == 0) return false;
                if (it.key() == "tipo") {
                    has_tipo = true;
                    if (it.value().is_string()) {
                        tipo_val = it.value().get<std::string>();
                    } else if (it.value().is_number_integer()) {
                        tipo_val = std::to_string(it.value().get<int64_t>());
                    } else {
                        return false;
                    }
                }
            }
            return has_tipo && (tipo_val == "1" || tipo_val == "2");
        } catch (...) {
            return false;
        }
    }

    if (trimmed.find('=') != std::string::npos) {
        auto pairs = ParseQueryPairs(trimmed);
        if (pairs.empty()) return false;
        bool has_tipo = false;
        std::string tipo_val;
        for (const auto& kv : pairs) {
            if (kPermittedKeys.count(kv.first) == 0) return false;
            if (kv.first == "tipo") {
                if (has_tipo) return false;  // reject duplicate tipo ambiguity
                has_tipo = true;
                tipo_val = kv.second;
            }
        }
        return has_tipo && (tipo_val == "1" || tipo_val == "2");
    }

    return false;
}

bool ReadOnlyRequestPolicy::IsMutatingUiLabel(std::string_view label) {
    detail::WarmupRegexLocale();
    std::string trimmed = TrimAscii(label);
    if (trimmed.empty()) return false;

    static const std::regex kMutatingLabelRe(
        R"((?:dar de baja|darse de baja|cancelar inscripci|retirar curso|desinscribirse|cerrar sesi))",
        std::regex::ECMAScript | std::regex::icase);
    if (std::regex_search(trimmed, kMutatingLabelRe)) return true;

    static const std::regex kConfirmBtnRe(
        R"(^(?:aceptar|confirmar|si|sí|yes|ok|enviar|guardar|aplicar)(?:\s|$))",
        std::regex::ECMAScript | std::regex::icase);
    return std::regex_search(trimmed, kConfirmBtnRe);
}

bool ReadOnlyRequestPolicy::IsReadOnlyPostPath(std::string_view pathname) {
    detail::WarmupRegexLocale();
    static const std::unordered_set<std::string> kExactPaths = {
        "/notifications/ajax/request_id",
        "/tda-consentimiento-informado/ajax/client/get_user_term_condition",
        "/tda-expediente-estudiantil/ajax/i18n",
        "/tda-expediente-estudiantil/ajax/i18n_validation",
        "/tda-feria-negocios/ajax/internationalization",
        "/tda-um/ajax/get_gaap_server",
        "/tda-um/ajax/internacionalization"
    };
    if (kExactPaths.count(std::string(pathname)) > 0) {
        return true;
    }
    static const std::regex kLoadMembersRe(
        R"(^/dotlrn/(?:[^/]+/)*ajax/load_members$)",
        std::regex::ECMAScript);
    return std::regex_match(std::string(pathname), kLoadMembersRe);
}

PolicyDecision ReadOnlyRequestPolicy::Evaluate(const PolicyRequestContext& ctx) const {
    ParsedUrl parsed = ParseHttpUrl(ctx.url);
    if (!parsed.valid) {
        return PolicyDecision::Block("unsupported-url");
    }

    std::string norm_path;
    if (!DecodeAndNormalizePolicyPath(parsed.path, norm_path)) {
        return PolicyDecision::Block("malformed-url-path");
    }
    parsed.path = std::move(norm_path);

    std::string verb = ctx.method.empty() ? "GET" : ctx.method;
    for (char& c : verb) {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }

    // Branch A: GraphQL endpoint
    if (IsGraphQlEndpoint(parsed)) {
        if (verb == "HEAD" || verb == "OPTIONS") {
            return PolicyDecision::Allow();
        }
        std::string gql_query;
        if (verb == "GET") {
            bool found_query = false;
            for (const auto& kv : ParseQueryPairs(parsed.query)) {
                if (kv.first == "query") {
                    if (found_query) {
                        return PolicyDecision::Block("graphql-not-proven-query");
                    }
                    found_query = true;
                    gql_query = kv.second;
                } else if (AsciiLower(kv.first) == "query") {
                    return PolicyDecision::Block("graphql-not-proven-query");
                }
            }
        } else if (verb == "POST") {
            std::string body = TrimAscii(ctx.post_data);
            if (!body.empty() && body.front() == '{') {
                try {
                    auto j = nlohmann::json::parse(body);
                    if (j.is_object() && j.contains("query") && j["query"].is_string()) {
                        gql_query = j["query"].get<std::string>();
                    }
                } catch (...) {
                    gql_query.clear();
                }
            } else if (body.find('=') != std::string::npos) {
                bool found_query = false;
                for (const auto& kv : ParseQueryPairs(body)) {
                    if (kv.first == "query") {
                        if (found_query) {
                            return PolicyDecision::Block("graphql-not-proven-query");
                        }
                        found_query = true;
                        gql_query = kv.second;
                    } else if (AsciiLower(kv.first) == "query") {
                        return PolicyDecision::Block("graphql-not-proven-query");
                    }
                }
            }
        } else {
            return PolicyDecision::Block("graphql-not-proven-query");
        }
        return IsReadOnlyGraphQL(gql_query)
                   ? PolicyDecision::Allow()
                   : PolicyDecision::Block("graphql-not-proven-query");
    }

    // Branch B: GET / HEAD / OPTIONS
    if (verb == "GET" || verb == "HEAD" || verb == "OPTIONS") {
        if (!IsTecDigitalOrigin(parsed)) {
            return PolicyDecision::Block("external-origin");
        }
        if (!IsPassiveResourceType(ctx.resource_type)) {
            if (IsMutatingPathOrQuery(parsed) || IsMutatingUiLabel(ctx.link_label)) {
                return PolicyDecision::Block("mutating-navigation-or-operation");
            }
        }
        return PolicyDecision::Allow();
    }

    // Branch C: Non-GET/HEAD/OPTIONS
    if (verb != "POST" || !IsTecDigitalOrigin(parsed)) {
        return PolicyDecision::Block("unreviewed-state-changing-method");
    }

    if (parsed.path == "/api/login/new-form/" || parsed.path == "/api/login/scanning/") {
        if (ctx.allow_login) {
            return PolicyDecision::Allow();
        }
        return PolicyDecision::Block("login-outside-authentication-phase");
    }

    if (IsReadOnlyPostPath(parsed.path)) {
        return PolicyDecision::Allow();
    }

    if (parsed.path == "/tda-prematricula/ajax/prematricula_est") {
        if (IsPrematriculaReadOnly(ctx.post_data)) {
            return PolicyDecision::Allow();
        }
        return PolicyDecision::Block("prematricula-operation-not-proven-read-only");
    }

    return PolicyDecision::Block("post-not-proven-read-only");
}

PolicyDecision ReadOnlyRequestPolicy::EvaluateHttpRequest(const HttpRequest& req,
                                                          bool allow_login) const {
    PolicyRequestContext ctx;
    ctx.url = req.url;
    ctx.method = req.method;
    ctx.resource_type = "document";
    ctx.post_data = req.body;
    ctx.allow_login = allow_login;
    return Evaluate(ctx);
}

PolicyEnforcingTransport::PolicyEnforcingTransport(IHttpTransport& inner,
                                                   ReadOnlyRequestPolicy policy)
    : inner_(inner), policy_(std::move(policy)) {}

HttpResponse PolicyEnforcingTransport::Execute(const HttpRequest& req) {
    HttpRequest current = req;
    const bool follow = req.follow_redirects;
    const int max_hops = (req.max_redirects > 0) ? req.max_redirects : 5;
    current.follow_redirects = false;

    for (int hop = 0; hop <= (follow ? max_hops : 0); ++hop) {
        PolicyDecision decision = policy_.EvaluateHttpRequest(current, AllowLogin());
        if (!decision.allowed) {
            HttpResponse blocked;
            blocked.transport_ok = false;
            blocked.final_url = current.url;
            blocked.error_message = "read-only-request-guard:" + decision.reason;
            return blocked;
        }

        HttpResponse resp = inner_.Execute(current);
        if (!follow || !resp.transport_ok) {
            return resp;
        }

        const int code = resp.status_code;
        const bool is_redirect =
            (code == 301 || code == 302 || code == 303 || code == 307 || code == 308);
        if (!is_redirect || resp.location.empty()) {
            return resp;
        }

        std::string next_url = ResolveRedirectLocation(current.url, resp.location);
        if (next_url.empty()) {
            resp.transport_ok = false;
            resp.error_message = "invalid redirect location";
            return resp;
        }
        if (!IsSameOrigin(current.url, next_url) || !policy_.IsAllowedOrigin(next_url)) {
            resp.transport_ok = false;
            resp.error_message = "OffOriginRedirect: blocked cross-origin redirect";
            return resp;
        }
        if (hop == max_hops) {
            resp.transport_ok = false;
            resp.error_message = "TooManyRedirects";
            return resp;
        }
        if (code == 301 || code == 302 || code == 303) {
            if (current.method != "GET" && current.method != "HEAD") {
                current.method = "GET";
                current.body.clear();
            }
        }
        current.url = std::move(next_url);
    }

    HttpResponse err;
    err.transport_ok = false;
    err.error_message = "TooManyRedirects";
    return err;
}

}  // namespace academic::tec_digital
