#define CROW_MAIN
#include "include/crow_all.h"
#include "smtp_mail.h"
#include "sqlite3.h"
#include "oops_concepts.h"
#include <ctime>
#include <filesystem>
#include <vector>
#include <windows.h>
#include <cctype>
#include <cstdlib>

using namespace std;
namespace fs = std::filesystem;

namespace
{
mutex g_otp_mutex;
string g_pending_otp;
chrono::steady_clock::time_point g_otp_expiry{};
string g_forgot_otp;
string g_forgot_username;
chrono::steady_clock::time_point g_forgot_expiry{};
bool g_forgot_pending = false;
string g_create_username, g_create_password, g_create_role, g_create_otp;
vector<string> g_create_subscriptions;
chrono::steady_clock::time_point g_create_expiry{};
const int OTP_TTL_SEC = 600;

/* Gmail SMTP (smtp.gmail.com:587) — edit here only; no environment variables. */
static string environmentValue(const char *name)
{
    const char *value = std::getenv(name);
    return value ? string(value) : string();
}

static string smtp_gmail_user() { return environmentValue("SMTP_GMAIL_ADDRESS"); }

static string smtp_gmail_app_password() { return environmentValue("SMTP_GMAIL_APP_PASSWORD"); }

static string smtp_otp_to_email() { return environmentValue("SMTP_RECIPIENT_EMAIL"); }

static string admin_username() { return environmentValue("APP_ADMIN_USERNAME"); }

static string admin_password() { return environmentValue("APP_ADMIN_PASSWORD"); }

string generate_otp_6()
{
    static mt19937 rng{random_device{}()};
    uniform_int_distribution<int> dist(100000, 999999);
    return to_string(dist(rng));
}

string urlDecode(const string &str)
{
    string res;
    for (size_t i = 0; i < str.size(); ++i)
    {
        if (str[i] == '%')
        {
            if (i + 2 < str.size())
            {
                int val = 0;
                stringstream ss(str.substr(i + 1, 2));
                ss >> hex >> val;
                res += char(val);
                i += 2;
            }
        }
        else if (str[i] == '+')
            res += ' ';
        else
            res += str[i];
    }
    return res;
}

fs::path getExecutableDir()
{
    char buffer[MAX_PATH];
    GetModuleFileNameA(NULL, buffer, MAX_PATH);
    return fs::path(buffer).parent_path();
}

fs::path findFileInParents(const fs::path &relative_path, int max_depth = 6, const fs::path &base = fs::current_path())
{
    fs::path current = fs::absolute(base);
    for (int depth = 0; depth <= max_depth; ++depth)
    {
        fs::path candidate = current / relative_path;
        if (fs::exists(candidate))
            return fs::absolute(candidate);
        fs::path parent = current.parent_path();
        if (parent == current || parent.empty())
            break;
        current = parent;
    }
    return fs::path();
}

fs::path findDbPath()
{
    fs::path exe_dir = getExecutableDir();
    vector<fs::path> candidates = {
        exe_dir.parent_path() / "company.db",
        exe_dir / "company.db",
        fs::current_path() / "company.db",
        fs::current_path().parent_path() / "company.db",
    };

    for (const auto &candidate : candidates)
    {
        if (!candidate.empty() && fs::exists(candidate))
            return fs::absolute(candidate);
    }

    for (const auto &base : candidates)
    {
        if (base.empty())
            continue;
        fs::path path = findFileInParents("company.db", 6, base.parent_path());
        if (!path.empty())
            return path;
    }

    return exe_dir / "company.db";
}

crow::response json_otp_response(bool ok, const string &msg, const string &err_key, int code = 200)
{
    crow::json::wvalue j;
    j["ok"] = ok;
    if (ok)
        j["message"] = msg;
    else
        j[err_key] = msg;
    return crow::response(code, std::move(j));
}
} // namespace

sqlite3 *DB;
map<string, string> sessions;
mutex g_sessions_mutex;

bool tableHasColumn(const string &table, const string &column)
{
    sqlite3_stmt *stmt = NULL;
    bool found = false;
    string query = "PRAGMA table_info(" + table + ");";
    if (sqlite3_prepare_v2(DB, query.c_str(), -1, &stmt, NULL) == SQLITE_OK)
    {
        while (sqlite3_step(stmt) == SQLITE_ROW)
        {
            const char *name = (const char *)sqlite3_column_text(stmt, 1);
            if (name && column == name)
            {
                found = true;
                break;
            }
        }
    }
    sqlite3_finalize(stmt);
    return found;
}

string load(string path)
{
    fs::path exe_dir = getExecutableDir();
    vector<fs::path> candidates = {
        exe_dir / path,
        exe_dir.parent_path() / path,
        exe_dir.parent_path().parent_path() / path,
        fs::current_path() / path,
        fs::current_path().parent_path() / path,
    };

    fs::path file_path;
    for (const auto &candidate : candidates)
    {
        if (!candidate.empty() && fs::exists(candidate))
        {
            file_path = fs::absolute(candidate);
            break;
        }
    }

    if (file_path.empty())
    {
        file_path = findFileInParents(path, 8, exe_dir);
        if (file_path.empty())
            file_path = findFileInParents(path, 8, fs::current_path());
    }

    if (file_path.empty())
        return "<h1>File not found: " + path + "</h1>";

    ifstream f(file_path, ios::binary);
    if (!f.is_open())
        return "<h1>File not found: " + file_path.string() + "</h1>";

    stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

string sqlEscape(const string &s)
{
    string r;
    for (char c : s)
    {
        if (c == '\'')
            r += "''";
        else
            r += c;
    }
    return r;
}

void initDB()
{
    fs::path db_file = findDbPath();
    fprintf(stderr, "Using database file: %s\n", db_file.string().c_str());
    sqlite3_open_v2(db_file.string().c_str(), &DB,
                    SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, NULL);

    sqlite3_exec(DB, "CREATE TABLE IF NOT EXISTS employees(username TEXT,password TEXT,role TEXT);", NULL, NULL, NULL);
    if (!tableHasColumn("employees", "role"))
        sqlite3_exec(DB, "ALTER TABLE employees ADD COLUMN role TEXT;", NULL, NULL, NULL);
    if (!tableHasColumn("employees", "email"))
        sqlite3_exec(DB, "ALTER TABLE employees ADD COLUMN email TEXT;", NULL, NULL, NULL);
    if (!tableHasColumn("employees", "weekly_holiday"))
        sqlite3_exec(DB, "ALTER TABLE employees ADD COLUMN weekly_holiday TEXT DEFAULT 'Sunday';", NULL, NULL, NULL);
    if (!tableHasColumn("employees", "photo"))
        sqlite3_exec(DB, "ALTER TABLE employees ADD COLUMN photo TEXT;", NULL, NULL, NULL);
    sqlite3_exec(DB, "CREATE TABLE IF NOT EXISTS subscriptions(name TEXT,password TEXT);", NULL, NULL, NULL);
    sqlite3_exec(DB, "CREATE TABLE IF NOT EXISTS emp_sub(username TEXT,subscription TEXT);", NULL, NULL, NULL);
    sqlite3_exec(DB, "CREATE TABLE IF NOT EXISTS attendance(username TEXT, date TEXT);", NULL, NULL, NULL);
    sqlite3_exec(DB, "ALTER TABLE attendance ADD COLUMN date TEXT;", NULL, NULL, NULL); // Ignore if already exists
    sqlite3_exec(DB, "CREATE TABLE IF NOT EXISTS pending_requests(username TEXT, subscription TEXT, status TEXT);", NULL, NULL, NULL);
    sqlite3_exec(DB, "CREATE TABLE IF NOT EXISTS pending_accounts(username TEXT, password TEXT, role TEXT, email TEXT, weekly_holiday TEXT, subscriptions TEXT);", NULL, NULL, NULL);
    if (!tableHasColumn("pending_accounts", "photo"))
        sqlite3_exec(DB, "ALTER TABLE pending_accounts ADD COLUMN photo TEXT;", NULL, NULL, NULL);
    sqlite3_exec(DB, "CREATE TABLE IF NOT EXISTS holiday_requests(username TEXT, date TEXT, reason TEXT, status TEXT DEFAULT 'pending');", NULL, NULL, NULL);
    sqlite3_exec(DB, "CREATE TABLE IF NOT EXISTS change_holiday_requests(username TEXT, new_holiday TEXT, status TEXT DEFAULT 'pending');", NULL, NULL, NULL);
    sqlite3_exec(DB, "UPDATE pending_requests SET subscription = replace(subscription, '%3D', '=') WHERE subscription LIKE '%3D%';", NULL, NULL, NULL);
    sqlite3_exec(DB, "UPDATE emp_sub SET subscription = replace(subscription, '%3D', '=') WHERE subscription LIKE '%3D%';", NULL, NULL, NULL);
    sqlite3_exec(DB, "UPDATE pending_requests SET subscription = substr(subscription, 2) WHERE subscription LIKE '=%';", NULL, NULL, NULL);
    sqlite3_exec(DB, "UPDATE emp_sub SET subscription = substr(subscription, 2) WHERE subscription LIKE '=%';", NULL, NULL, NULL);

    // Create uploads directory for photos
    fs::path uploads_dir = fs::current_path() / "uploads";
    if (!fs::exists(uploads_dir)) {
        fs::create_directory(uploads_dir);
    }
}

string val(string body, string key)
{
    size_t s = body.find(key + "=");
    if (s == string::npos)
        return "";
    s += key.length() + 1;

    size_t e = body.find("&", s);
    if (e == string::npos)
        e = body.length();

    return urlDecode(body.substr(s, e - s));
}

string normalizeSubscription(const string &subscription)
{
    string out = subscription;
    if (!out.empty() && out[0] == '=')
        out.erase(0, 1);
    while (!out.empty() && isspace((unsigned char)out.front()))
        out.erase(0, 1);
    while (!out.empty() && isspace((unsigned char)out.back()))
        out.pop_back();
    return out;
}

vector<string> formValues(const string &body, const string &key)
{
    vector<string> out;
    string needle = key + "=";
    size_t pos = 0;
    while (true)
    {
        size_t s = body.find(needle, pos);
        if (s == string::npos)
            break;
        s += needle.size();
        size_t e = body.find("&", s);
        if (e == string::npos)
            e = body.length();
        out.push_back(urlDecode(body.substr(s, e - s)));
        pos = e + 1;
    }
    return out;
}

string getUser(const crow::request &req)
{
    auto cookie = req.get_header_value("Cookie");
    size_t pos = cookie.find("session=");
    if (pos == string::npos)
        return "";

    string id = cookie.substr(pos + 8);
    size_t semi = id.find(';');
    if (semi != string::npos)
        id = id.substr(0, semi);

    lock_guard<mutex> lock(g_sessions_mutex);
    auto it = sessions.find(id);
    if (it != sessions.end())
        return it->second;

    return "";
}

crow::response redirect(const string &loc)
{
    crow::response res;
    res.code = 302;
    res.add_header("Location", loc);
    return res;
}

string escapeJS(const string &value)
{
    string escaped;
    for (char c : value)
    {
        if (c == '\\') escaped += "\\\\";
        else if (c == '\'') escaped += "\\'";
        else if (c == '"') escaped += "\\\"";
        else if (c == '\n') escaped += "\\n";
        else if (c == '\r') escaped += "\\r";
        else escaped += c;
    }
    return escaped;
}

crow::response alertAndRedirect(const string &message, const string &location)
{
    string html = "<!DOCTYPE html><html><head><meta charset=\"UTF-8\"><link rel=\"stylesheet\" href=\"/static/style.css\"></head><body>";
    html += "<div class=\"main\"><div class=\"card center-box\"><h2>" + message + "</h2>";
    html += "<p class=\"hint\"><a href=\"" + location + "\"><button type=\"button\">Continue</button></a></p>";
    html += "</div></div>";
    html += "</body></html>";
    return crow::response(200, html);
}

void replacePlaceholder(string &html, const string &placeholder, const string &value)
{
    size_t pos = html.find(placeholder);
    if (pos != string::npos)
        html.replace(pos, placeholder.size(), value);
}

string htmlEscape(const string &value)
{
    string escaped;
    for (char c : value)
    {
        switch (c)
        {
        case '&': escaped += "&amp;"; break;
        case '<': escaped += "&lt;"; break;
        case '>': escaped += "&gt;"; break;
        case '"': escaped += "&quot;"; break;
        case '\'': escaped += "&#39;"; break;
        default: escaped += c; break;
        }
    }
    return escaped;
}

bool parseDate(const string &date, int &year, int &month, int &day)
{
    if (sscanf(date.c_str(), "%d-%d-%d", &year, &month, &day) != 3)
        return false;
    return year >= 1 && month >= 1 && month <= 12 && day >= 1 && day <= 31;
}

bool isDateBefore(const string &date, int year, int month, int day)
{
    int y, m, d;
    if (!parseDate(date, y, m, d))
        return false;
    if (y != year)
        return y < year;
    if (m != month)
        return m < month;
    return d < day;
}

string dayNameFromDate(const string &date)
{
    int year, month, day;
    if (!parseDate(date, year, month, day))
        return string();
    tm t = {};
    t.tm_year = year - 1900;
    t.tm_mon = month - 1;
    t.tm_mday = day;
    mktime(&t);
    const char *days[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
    return days[t.tm_wday];
}

bool isAdminUser(const string &u) { return u == "dhamu"; }

bool adminOk(const crow::request &req) { return isAdminUser(getUser(req)); }

bool employeeOk(const crow::request &req)
{
    string u = getUser(req);
    return !u.empty() && !isAdminUser(u);
}

static bool employeeUsernameExists(const string &u)
{
    bool found = false;
    auto cb = [](void *d, int, char **, char **)
    {
        *(bool *)d = true;
        return 0;
    };
    sqlite3_exec(DB, ("SELECT 1 FROM employees WHERE username='" + sqlEscape(u) + "' LIMIT 1;").c_str(), cb, &found, NULL);
    return found;
}

static int cbConcatOptions(void *d, int argc, char **argv, char **)
{
    string *s = (string *)d;
    if (argc > 0 && argv[0])
    {
        string name(argv[0]);
        // Skip invalid subscription names
        if (name.empty() || name == "}" || name.find_first_not_of(" \t\n\r") == string::npos)
            return 0;
        *s += "<option value=\"" + name + "\">" + name + "</option>";
    }
    return 0;
}

static int cbConcatSubsCheck(void *d, int argc, char **argv, char **)
{
    string *s = (string *)d;
    if (argc > 0 && argv[0])
    {
        string name(argv[0]);
        // Skip invalid subscription names
        if (name.empty() || name == "}" || name.find_first_not_of(" \t\n\r") == string::npos)
            return 0;
        *s += "<label class=\"sub-pick\"><input type=\"checkbox\" name=\"subscription\" value=\"" + name + "\"> " + name + "</label><br>";
    }
    return 0;
}

string employeeOptionsHtml()
{
    string opts;
    sqlite3_exec(DB, "SELECT username FROM employees ORDER BY username;", cbConcatOptions, &opts, NULL);
    return opts;
}

string employeeOptionsHtmlSelected(const string &selected)
{
    string opts;
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(DB, "SELECT username FROM employees ORDER BY username;", -1, &st, NULL) != SQLITE_OK)
        return opts;
    while (sqlite3_step(st) == SQLITE_ROW)
    {
        const char *nm = (const char *)sqlite3_column_text(st, 0);
        string name(nm ? nm : "");
        opts += "<option value=\"" + name + "\"";
        if (name == selected)
            opts += " selected";
        opts += ">" + name + "</option>";
    }
    sqlite3_finalize(st);
    return opts;
}

string getEmployeeWeeklyHoliday(const string &username)
{
    string holiday = "Sunday";
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(DB, "SELECT weekly_holiday FROM employees WHERE username=?;", -1, &stmt, NULL) == SQLITE_OK)
    {
        sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_STATIC);
        if (sqlite3_step(stmt) == SQLITE_ROW)
        {
            const char *text = (const char *)sqlite3_column_text(stmt, 0);
            if (text && *text)
                holiday = string(text);
        }
    }
    sqlite3_finalize(stmt);
    return holiday;
}

string buildEmployeeSubCheckboxes(const string &username)
{
    string out;
    string eu = sqlEscape(username);
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(DB, "SELECT name FROM subscriptions ORDER BY name;", -1, &st, NULL) != SQLITE_OK)
        return out;
    while (sqlite3_step(st) == SQLITE_ROW)
    {
        const char *nm = (const char *)sqlite3_column_text(st, 0);
        if (!nm)
            continue;
        string name(nm);
        // Skip invalid subscription names
        if (name.empty() || name == "}" || name.find_first_not_of(" \t\n\r") == string::npos)
            continue;
        bool has = false;
        string q = "SELECT 1 FROM emp_sub WHERE username='" + eu + "' AND subscription='" + sqlEscape(name) + "' LIMIT 1;";
        auto cb = [](void *d, int, char **, char **)
        {
            *(bool *)d = true;
            return 0;
        };
        sqlite3_exec(DB, q.c_str(), cb, &has, NULL);
        out += "<label class=\"sub-pick\"><input type=\"checkbox\" name=\"subscription\" value=\"" + name + "\"";
        if (has)
            out += " checked";
        out += "> " + name + "</label><br>";
    }
    sqlite3_finalize(st);
    return out;
}

string subscriptionOptionsHtml()
{
    string opts;
    sqlite3_exec(DB, "SELECT name FROM subscriptions ORDER BY name;", cbConcatOptions, &opts, NULL);
    return opts;
}

string generateAttendanceCalendar(const string &username)
{
    string calendar;

    set<string> presentDates;
    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(DB, "SELECT date FROM attendance WHERE username=? ORDER BY date;", -1, &stmt, NULL) == SQLITE_OK)
    {
        sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_STATIC);
        while (sqlite3_step(stmt) == SQLITE_ROW)
        {
            const char *dateStr = (const char *)sqlite3_column_text(stmt, 0);
            if (dateStr)
                presentDates.insert(string(dateStr));
        }
    }
    sqlite3_finalize(stmt);

    set<string> approvedDates;
    map<string, string> approvedReasons;
    if (sqlite3_prepare_v2(DB, "SELECT date, reason FROM holiday_requests WHERE username=? AND status='approved';", -1, &stmt, NULL) == SQLITE_OK)
    {
        sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_STATIC);
        while (sqlite3_step(stmt) == SQLITE_ROW)
        {
            const char *dateStr = (const char *)sqlite3_column_text(stmt, 0);
            const char *reason = (const char *)sqlite3_column_text(stmt, 1);
            if (dateStr)
            {
                string dateVal(dateStr);
                approvedDates.insert(dateVal);
                approvedReasons[dateVal] = reason ? string(reason) : "Approved holiday";
            }
        }
    }
    sqlite3_finalize(stmt);

    string weeklyHoliday = getEmployeeWeeklyHoliday(username);

    const int companyStartYear = 2026;
    const int companyStartMonth = 4;
    const int companyStartDay = 6;

    time_t now = time(0);
    tm *ltm = localtime(&now);
    int currentYear = 1900 + ltm->tm_year;
    int currentMonth = 1 + ltm->tm_mon;
    int currentDay = ltm->tm_mday;

    calendar += "<div class=\"attendance-calendar-container\">";

    for (int month = 1; month <= currentMonth; month++)
    {
        calendar += "<div class=\"month-calendar\">";
        calendar += "<h3>";
        const char *months[] = {"", "January", "February", "March", "April", "May", "June", 
                                 "July", "August", "September", "October", "November", "December"};
        calendar += months[month];
        calendar += " " + to_string(currentYear) + "</h3>";

        calendar += "<div class=\"calendar-grid\">";
        const char *dow[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
        for (int i = 0; i < 7; i++)
            calendar += "<div class=\"dow-header\">" + string(dow[i]) + "</div>";

        int daysInMonth;
        if (month == 2)
            daysInMonth = ((currentYear % 4 == 0 && currentYear % 100 != 0) || currentYear % 400 == 0) ? 29 : 28;
        else
            daysInMonth = (month == 4 || month == 6 || month == 9 || month == 11) ? 30 : 31;

        tm timeInfo = {0};
        timeInfo.tm_year = currentYear - 1900;
        timeInfo.tm_mon = month - 1;
        timeInfo.tm_mday = 1;
        mktime(&timeInfo);
        int startDow = timeInfo.tm_wday;

        for (int i = 0; i < startDow; i++)
            calendar += "<div class=\"calendar-day empty\"></div>";

        for (int day = 1; day <= daysInMonth; day++)
        {
            char dateStr[11];
            sprintf(dateStr, "%04d-%02d-%02d", currentYear, month, day);
            string dateVal(dateStr);

            bool isPresent = presentDates.count(dateVal) > 0;
            bool isApprovedHoliday = approvedDates.count(dateVal) > 0;
            bool isWeeklyHoliday = false;
            bool isBeforeCompanyStart = isDateBefore(dateVal, companyStartYear, companyStartMonth, companyStartDay);
            {
                tm check = {0};
                check.tm_year = currentYear - 1900;
                check.tm_mon = month - 1;
                check.tm_mday = day;
                mktime(&check);
                const char *days[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
                isWeeklyHoliday = weeklyHoliday == days[check.tm_wday];
            }

            bool isToday = (month == currentMonth && day == currentDay);
            bool isPast = (month < currentMonth) || (month == currentMonth && day < currentDay);
            string dayClass = "future";
            string tooltip = dateVal;

            if (isBeforeCompanyStart)
            {
                dayClass = "company-not-created";
                tooltip += " - Company not created before April 6 2026";
            }
            else if (isApprovedHoliday)
            {
                dayClass = "approved-holiday";
                tooltip += " - Approved holiday: " + approvedReasons[dateVal];
            }
            else if (isWeeklyHoliday)
            {
                dayClass = "weekly-holiday";
                tooltip += " - Weekly holiday";
            }
            else if (isPresent)
            {
                dayClass = "present";
                tooltip += " - Present";
            }
            else if (isPast)
            {
                dayClass = "absent";
                tooltip += " - Absent";
            }
            else if (isToday)
            {
                dayClass = "today";
                tooltip += " - Today";
            }

            calendar += "<div class=\"calendar-day " + dayClass + "\" title=\"" + tooltip + "\">";
            calendar += to_string(day);
            calendar += "</div>";
        }

        calendar += "</div>";
        calendar += "</div>";
    }

    calendar += "</div>";
    calendar += "<div class=\"legend-row\">";
    calendar += "<div class=\"legend-line\"><span class=\"legend-dot legend-present\"></span>Present</div>";
    calendar += "<div class=\"legend-line\"><span class=\"legend-dot legend-absent\"></span>Absent</div>";
    calendar += "<div class=\"legend-line\"><span class=\"legend-dot legend-weekly\"></span>Weekly holiday</div>";
    calendar += "<div class=\"legend-line\"><span class=\"legend-dot legend-approved\"></span>Approved holiday</div>";
    calendar += "</div>";
    return calendar;
}

int main() {
    curl_smtp_global_init();
    atexit(curl_smtp_global_cleanup);

    crow::SimpleApp app;
    initDB();

    CROW_ROUTE(app, "/static/<string>")
    ([](string file)
     { return crow::response(load("static/" + file)); });

    CROW_ROUTE(app, "/avatar/<string>")
    ([](string username)
    {
        if (username.empty() || username.length() > 50)
            return crow::response(400, "Invalid username");
        char firstLetter = toupper(username[0]);
        string colors[] = {"3b82f6", "8b5cf6", "ec4899", "f59e0b", "10b981", "06b6d4", "6366f1"};
        int colorIdx = (int)username[0] % 7;
        string color = colors[colorIdx];
        string svg = "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"200\" height=\"200\">";
        svg += "<rect width=\"200\" height=\"200\" fill=\"#" + color + "\"/>";
        svg += "<text x=\"100\" y=\"120\" font-size=\"80\" font-weight=\"bold\" fill=\"white\" text-anchor=\"middle\" font-family=\"Arial\">" + string(1, firstLetter) + "</text>";
        svg += "</svg>";
        crow::response res(svg);
        res.add_header("Content-Type", "image/svg+xml");
        return res;
    });

    CROW_ROUTE(app, "/uploads/<string>")
    ([](string file)
     { return crow::response(load("uploads/" + file)); });

    std::cerr << "ROUTE INIT" << std::endl;

    CROW_ROUTE(app, "/")([]
                         { return crow::response(load("templates/login.html")); });

    CROW_ROUTE(app, "/admin")([](const crow::request &req)
                              {
        if (!adminOk(req)) return redirect("/");
        return crow::response(load("templates/admin_dashboard.html")); });

    CROW_ROUTE(app, "/employee")([](const crow::request &req)
                                 {
        if (!employeeOk(req)) return redirect("/");
        string html = load("templates/employee_dashboard.html");
        string user = getUser(req);
        string weeklyHoliday = getEmployeeWeeklyHoliday(user);
        replacePlaceholder(html, "{{WEEKLY_HOLIDAY}}", weeklyHoliday);
        replacePlaceholder(html, "{{USERNAME}}", user);

        // Get photo
        sqlite3_stmt *stmt;
        sqlite3_prepare_v2(DB, "SELECT photo FROM employees WHERE username=?;", -1, &stmt, NULL);
        sqlite3_bind_text(stmt, 1, user.c_str(), -1, SQLITE_STATIC);
        string photo = "/avatar/" + user; // Default to avatar with first letter
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            const char* p = (const char*)sqlite3_column_text(stmt, 0);
            if (p && strlen(p) > 0) photo = p;
        }
        sqlite3_finalize(stmt);
        replacePlaceholder(html, "{{PHOTO}}", photo);

        return crow::response(html);
    });

    CROW_ROUTE(app, "/change_profile_photo").methods("POST"_method)([](const crow::request &req)
                                                                     {
        if (!employeeOk(req)) return redirect("/");
        string user = getUser(req);
        crow::multipart::message msg(req);
        auto photoPart = msg.get_part_by_name("photo");
        if (photoPart.body.empty())
            return alertAndRedirect("Please select a profile photo.", "/employee");

        string filename = user + "_photo.jpg";
        auto cd = photoPart.get_header_object("Content-Disposition");
        auto it = cd.params.find("filename");
        if (it != cd.params.end() && !it->second.empty()) {
            string orig_filename = it->second;
            size_t dot_pos = orig_filename.find_last_of('.');
            if (dot_pos != string::npos)
                filename = user + "_photo" + orig_filename.substr(dot_pos);
        }

        fs::path photo_file = fs::current_path() / "uploads" / filename;
        ofstream out(photo_file, ios::binary);
        out.write(photoPart.body.c_str(), photoPart.body.size());
        out.close();
        string photo_url = "/uploads/" + filename;

        sqlite3_stmt *stmt;
        sqlite3_prepare_v2(DB, "UPDATE employees SET photo=? WHERE username=?;", -1, &stmt, NULL);
        sqlite3_bind_text(stmt, 1, photo_url.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, user.c_str(), -1, SQLITE_STATIC);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);

        return alertAndRedirect("Profile photo updated.", "/employee");
    });

    CROW_ROUTE(app, "/login").methods("POST"_method)([](const crow::request &req)
                                                     {
        string u = val(req.body, "username");
        string p = val(req.body, "password");
        string sid = to_string(rand());

        if (!admin_username().empty() && u == admin_username() && p == admin_password())
        {
            {
                lock_guard<mutex> lock(g_sessions_mutex);
                sessions[sid] = u;
            }
            crow::response res;
            res.code = 302;
            res.add_header("Set-Cookie", "session=" + sid + "; Path=/; HttpOnly");
            res.add_header("Location", "/admin");
            return res;
        }

        bool found = false;
        auto cb = [](void *d, int, char **, char **)
        {
            *(bool *)d = true;
            return 0;
        };

        string q = "SELECT * FROM employees WHERE username='" + sqlEscape(u) + "' AND password='" + sqlEscape(p) + "';";
        sqlite3_exec(DB, q.c_str(), cb, &found, NULL);

        if (found)
        {
            {
                lock_guard<mutex> lock(g_sessions_mutex);
                sessions[sid] = u;
            }
            crow::response res;
            res.code = 302;
            res.add_header("Set-Cookie", "session=" + sid + "; Path=/; HttpOnly");
            res.add_header("Location", "/employee");
            return res;
        }

        return crow::response(load("templates/invalid.html")); });

    CROW_ROUTE(app, "/logout")
    ([](const crow::request &req)
     {
        auto cookie = req.get_header_value("Cookie");
        size_t pos = cookie.find("session=");
        if (pos != string::npos)
        {
            string id = cookie.substr(pos + 8);
            size_t semi = id.find(';');
            if (semi != string::npos)
                id = id.substr(0, semi);
            lock_guard<mutex> lock(g_sessions_mutex);
            sessions.erase(id);
        }
        return redirect("/"); });

    CROW_ROUTE(app, "/add_employee")([](const crow::request &req)
                                     {
        if (!adminOk(req)) return redirect("/");
        string html = load("templates/add_employee.html");
        string opts;
        sqlite3_exec(DB, "SELECT name FROM subscriptions ORDER BY name;", cbConcatSubsCheck, &opts, NULL);
        size_t p = html.find("{{SUBS}}");
        if (p != string::npos)
            html.replace(p, 8, opts);
        return crow::response(html); });

    CROW_ROUTE(app, "/add_employee").methods("POST"_method)([](const crow::request &req)
                                                            {
        if (!adminOk(req)) return redirect("/");
        crow::multipart::message msg(req);
        string u = msg.get_part_by_name("username").body;
        string p = msg.get_part_by_name("password").body;
        string e = msg.get_part_by_name("email").body;
        string r = msg.get_part_by_name("role").body;
        string h = msg.get_part_by_name("weekly_holiday").body;
        string photo_path;
        vector<string> subs;

        auto photoPart = msg.get_part_by_name("photo");
        if (!photoPart.body.empty()) {
            string filename = u + "_photo.jpg";
            auto cd = photoPart.get_header_object("Content-Disposition");
            auto it = cd.params.find("filename");
            if (it != cd.params.end() && !it->second.empty()) {
                string orig_filename = it->second;
                size_t dot_pos = orig_filename.find_last_of(".");
                if (dot_pos != string::npos)
                    filename = u + "_photo" + orig_filename.substr(dot_pos);
            }
            fs::path photo_file = fs::current_path() / "uploads" / filename;
            ofstream out(photo_file, ios::binary);
            out.write(photoPart.body.c_str(), photoPart.body.size());
            out.close();
            photo_path = "/uploads/" + filename;
        }

        for (auto& part : msg.parts) {
            auto cd = part.get_header_object("Content-Disposition");
            auto it = cd.params.find("name");
            if (it == cd.params.end())
                continue;
            string field_name = it->second;
            if (field_name == "subscription")
                subs.push_back(part.body);
        }

        if (u.empty() || p.empty() || e.empty() || r.empty() || h.empty() || photo_path.empty())
            return alertAndRedirect("Missing fields. Please fill all required values.", "/add_employee");

        // Add employee to database
        string q = "INSERT INTO employees (username,password,role,email,weekly_holiday,photo) VALUES('" + sqlEscape(u) + "','" + sqlEscape(p) + "','" + sqlEscape(r) + "','" + sqlEscape(e) + "','" + sqlEscape(h) + "','" + sqlEscape(photo_path) + "');";
        sqlite3_exec(DB, q.c_str(), NULL, NULL, NULL);

        string eu = sqlEscape(u);
        for (const string &sub : subs)
        {
            if (sub.empty()) continue;
            string q = "INSERT INTO emp_sub VALUES('" + eu + "','" + sqlEscape(sub) + "');";
            sqlite3_exec(DB, q.c_str(), NULL, NULL, NULL);
        }

        return alertAndRedirect("Employee added", "/manage_employees"); });

    CROW_ROUTE(app, "/manage_employees")([](const crow::request &req)
                                         {
        if (!adminOk(req)) return redirect("/");
        string html = load("templates/manage_employees.html");
        string data;

        auto cb = [](void *d, int argc, char **argv, char **)
        {
            string *s = (string *)d;
            if (argc >= 3 && argv[0] && argv[1])
            {
                string username = string(argv[0]);
                string photo = (argv[2] && strlen(argv[2]) > 0) ? string(argv[2]) : ("/avatar/" + username);
                *s += "<tr><td><img src=\"" + photo + "\" class=\"employee-photo-thumb\"></td>";
                *s += "<td>" + username + "</td>";
                *s += "<td>" + string(argv[1]) + "</td></tr>";
            }
            return 0;
        };

        sqlite3_exec(DB, "SELECT username,role,photo FROM employees ORDER BY username;", cb, &data, NULL);
        size_t pos = html.find("{{DATA}}");
        if (pos != string::npos)
            html.replace(pos, 8, data);
        return crow::response(html); });

    CROW_ROUTE(app, "/manage_employee_subscriptions")([](const crow::request &req)
                                                      {
        if (!adminOk(req)) return redirect("/");
        string sel;
        char *qu = req.url_params.get("username");
        if (qu)
            sel = string(qu);
        string html = load("templates/manage_employee_subscriptions.html");
        string empOpts = employeeOptionsHtmlSelected(sel);
        size_t pos;
        while ((pos = html.find("{{EMP_OPTIONS}}")) != string::npos)
            html.replace(pos, 14, empOpts);
        replacePlaceholder(html, "{{SUB_CHECKS}}", buildEmployeeSubCheckboxes(sel));
        return crow::response(html); });

    CROW_ROUTE(app, "/manage_employee_subscriptions").methods("POST"_method)([](const crow::request &req)
                                                                             {
        if (!adminOk(req)) return redirect("/");
        string u = val(req.body, "username");
        vector<string> subs = formValues(req.body, "subscription");
        string eu = sqlEscape(u);
        sqlite3_exec(DB, ("DELETE FROM emp_sub WHERE username='" + eu + "';").c_str(), NULL, NULL, NULL);
        for (const string &sub : subs)
        {
            if (sub.empty()) continue;
            string q = "INSERT INTO emp_sub VALUES('" + eu + "','" + sqlEscape(sub) + "');";
            sqlite3_exec(DB, q.c_str(), NULL, NULL, NULL);
        }
        return alertAndRedirect("Subscriptions updated", "/manage_employees"); });

    CROW_ROUTE(app, "/delete_employee")([](const crow::request &req)
                                      {
        if (!adminOk(req)) return redirect("/");
        string html = load("templates/delete_employee.html");
        replacePlaceholder(html, "{{EMP_OPTIONS}}", employeeOptionsHtml());
        return crow::response(html); });

    CROW_ROUTE(app, "/delete_employee").methods("POST"_method)([](const crow::request &req)
                                                               {
        if (!adminOk(req)) return redirect("/");
        string u = val(req.body, "username");
        string eu = sqlEscape(u);
        sqlite3_exec(DB, ("DELETE FROM emp_sub WHERE username='" + eu + "';").c_str(), NULL, NULL, NULL);
        sqlite3_exec(DB, ("DELETE FROM attendance WHERE username='" + eu + "';").c_str(), NULL, NULL, NULL);
        sqlite3_exec(DB, ("DELETE FROM employees WHERE username='" + eu + "';").c_str(), NULL, NULL, NULL);
        return alertAndRedirect("Employee deleted", "/manage_employees"); });

    CROW_ROUTE(app, "/edit_employee")([](const crow::request &req)
                                     {
        if (!adminOk(req)) return redirect("/");
        string html = load("templates/edit_employee.html");
        replacePlaceholder(html, "{{EMP_OPTIONS}}", employeeOptionsHtml());
        return crow::response(html); });

    CROW_ROUTE(app, "/edit_employee").methods("POST"_method)([](const crow::request &req)
                                                             {
        if (!adminOk(req)) return redirect("/");
        string u = val(req.body, "username");
        string r = val(req.body, "role");
        string q = "UPDATE employees SET role='" + sqlEscape(r) + "' WHERE username='" + sqlEscape(u) + "';";
        sqlite3_exec(DB, q.c_str(), NULL, NULL, NULL);
        return alertAndRedirect("Employee role updated", "/manage_employees"); });

    CROW_ROUTE(app, "/change_password")([](const crow::request &req)
                                        {
        if (!adminOk(req)) return redirect("/");
        string html = load("templates/change_password.html");
        replacePlaceholder(html, "{{EMP_OPTIONS}}", employeeOptionsHtml());
        return crow::response(html); });

    CROW_ROUTE(app, "/change_password").methods("POST"_method)([](const crow::request &req)
                                                               {
        if (!adminOk(req)) return redirect("/");
        string u = val(req.body, "username");
        string p = val(req.body, "password");
        string q = "UPDATE employees SET password='" + sqlEscape(p) + "' WHERE username='" + sqlEscape(u) + "';";
        sqlite3_exec(DB, q.c_str(), NULL, NULL, NULL);
        return alertAndRedirect("Password changed", "/manage_employees"); });

    CROW_ROUTE(app, "/manage_subscriptions")([](const crow::request &req)
                                             {
        if (!adminOk(req)) return redirect("/");
        string html = load("templates/manage_subscriptions.html");
        string data;
        auto cb = [](void *d, int argc, char **argv, char **)
        {
            string *s = (string *)d;
            if (argc >= 1 && argv[0])
                *s += "<tr><td>" + string(argv[0]) + "</td></tr>";
            return 0;
        };
        sqlite3_exec(DB, "SELECT name FROM subscriptions ORDER BY name;", cb, &data, NULL);
        replacePlaceholder(html, "{{DATA}}", data);
        return crow::response(html); });

    CROW_ROUTE(app, "/add_subscription")([](const crow::request &req)
                                        {
        if (!adminOk(req)) return redirect("/");
        return crow::response(load("templates/add_subscription.html")); });

    CROW_ROUTE(app, "/add_subscription").methods("POST"_method)([](const crow::request &req)
                                                                {
        if (!adminOk(req)) return redirect("/");
        string n = val(req.body, "name");
        string p = val(req.body, "password");
        string q = "INSERT INTO subscriptions VALUES('" + sqlEscape(n) + "','" + sqlEscape(p) + "');";
        sqlite3_exec(DB, q.c_str(), NULL, NULL, NULL);
        return alertAndRedirect("Subscription added", "/manage_subscriptions"); });

    CROW_ROUTE(app, "/remove_subscription")([](const crow::request &req)
                                           {
        if (!adminOk(req)) return redirect("/");
        string html = load("templates/remove_subscription.html");
        replacePlaceholder(html, "{{SUB_OPTIONS}}", subscriptionOptionsHtml());
        return crow::response(html); });

    CROW_ROUTE(app, "/remove_subscription").methods("POST"_method)([](const crow::request &req)
                                                                   {
        if (!adminOk(req)) return redirect("/");
        string n = val(req.body, "name");
        string en = sqlEscape(n);
        sqlite3_exec(DB, ("DELETE FROM emp_sub WHERE subscription='" + en + "';").c_str(), NULL, NULL, NULL);
        sqlite3_exec(DB, ("DELETE FROM subscriptions WHERE name='" + en + "';").c_str(), NULL, NULL, NULL);
        return alertAndRedirect("Subscription removed", "/manage_subscriptions"); });

    CROW_ROUTE(app, "/edit_subscription")([](const crow::request &req)
                                         {
        if (!adminOk(req)) return redirect("/");
        string html = load("templates/edit_subscription_pick.html");
        replacePlaceholder(html, "{{SUB_OPTIONS}}", subscriptionOptionsHtml());
        return crow::response(html); });

    CROW_ROUTE(app, "/edit_subscription").methods("POST"_method)([](const crow::request &req)
                                                                 {
        if (!adminOk(req)) return redirect("/");
        string step = val(req.body, "step");
        if (step == "1")
        {
            string oldName = val(req.body, "old_name");
            string html = load("templates/edit_subscription_form.html");
            size_t p = html.find("{{OLD_NAME}}");
            if (p != string::npos)
                html.replace(p, 12, oldName);
            return crow::response(html);
        }
        if (step == "2")
        {
            string oldName = val(req.body, "old_name");
            string newName = val(req.body, "new_name");
            string newPass = val(req.body, "new_password");
            string eo = sqlEscape(oldName);
            string en = sqlEscape(newName);
            string ep = sqlEscape(newPass);
            sqlite3_exec(DB, ("UPDATE subscriptions SET name='" + en + "', password='" + ep + "' WHERE name='" + eo + "';").c_str(), NULL, NULL, NULL);
            sqlite3_exec(DB, ("UPDATE emp_sub SET subscription='" + en + "' WHERE subscription='" + eo + "';").c_str(), NULL, NULL, NULL);
            return alertAndRedirect("Subscription updated", "/manage_subscriptions");
        }
        return redirect("/manage_subscriptions"); });

    CROW_ROUTE(app, "/attendance")([](const crow::request &req)
                                   {
        if (!adminOk(req)) return redirect("/");
        string html = load("templates/attendance.html");
        replacePlaceholder(html, "{{EMP_OPTIONS}}", employeeOptionsHtml());
        replacePlaceholder(html, "{{RESULT}}", "<p class=\"hint\">Select an employee and click View to see how many times they marked present.</p>");
        return crow::response(html); });

    CROW_ROUTE(app, "/attendance").methods("POST"_method)([](const crow::request &req)
                                                          {
        if (!adminOk(req)) return redirect("/");
        string u = val(req.body, "username");
        string eu = sqlEscape(u);

        string html = load("templates/attendance.html");
        replacePlaceholder(html, "{{EMP_OPTIONS}}", employeeOptionsHtml());
        
        string calendar = generateAttendanceCalendar(u);
        replacePlaceholder(html, "{{RESULT}}", calendar);
        return crow::response(html); });

    CROW_ROUTE(app, "/view_my_attendance")([](const crow::request &req)
                                           {
        if (!employeeOk(req)) return redirect("/");
        string u = getUser(req);
        
        string html = load("templates/view_my_attendance.html");
        string calendar = generateAttendanceCalendar(u);
        replacePlaceholder(html, "{{CALENDAR}}", calendar);
        
        return crow::response(html); });

    CROW_ROUTE(app, "/mark_attendance")([](const crow::request &req)
                                        {
        if (!employeeOk(req)) return redirect("/");
        return crow::response(load("templates/mark_attendance.html")); });

    CROW_ROUTE(app, "/mark_attendance").methods("POST"_method)([](const crow::request &req)
                                                               {
        if (!employeeOk(req)) return redirect("/");
        string u = getUser(req);

        // Get today's date
        time_t now = time(0);
        tm *ltm = localtime(&now);
        char date[11];
        sprintf(date, "%04d-%02d-%02d", 1900 + ltm->tm_year, 1 + ltm->tm_mon, ltm->tm_mday);
        string today = date;

        // Check if today is the employee's weekly holiday
        string weeklyHoliday = getEmployeeWeeklyHoliday(u);
        const char *days[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
        tm todayTm = *ltm;
        int dow = todayTm.tm_wday;
        if (dow >= 0 && dow < 7 && weeklyHoliday == days[dow])
        {
            return alertAndRedirect("Today is your holiday. Enjoy!", "/employee");
        }

        // Check if already marked
        sqlite3_stmt *stmt;
        sqlite3_prepare_v2(DB, "SELECT 1 FROM attendance WHERE username=? AND date=?;", -1, &stmt, NULL);
        sqlite3_bind_text(stmt, 1, u.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, today.c_str(), -1, SQLITE_STATIC);
        int rc = sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        if (rc == SQLITE_ROW)
        {
            string html = load("templates/already_marked.html");
            return crow::response(200, html);
        }

        // Insert
        sqlite3_prepare_v2(DB, "INSERT INTO attendance VALUES(?, ?);", -1, &stmt, NULL);
        sqlite3_bind_text(stmt, 1, u.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, today.c_str(), -1, SQLITE_STATIC);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);

        return alertAndRedirect("Attendance marked", "/employee"); });

    CROW_ROUTE(app, "/view_subscriptions")
    ([](const crow::request &req)
     {
        if (!employeeOk(req)) return redirect("/");
        string u = getUser(req);
        string html = load("templates/view_subscriptions.html");
        string data;

        auto cb = [](void *d, int argc, char **argv, char **)
        {
            string *s = (string *)d;
            if (argc >= 2 && argv[0] && argv[1])
                *s += "<tr><td>" + string(argv[0]) + "</td><td>" + string(argv[1]) + "</td></tr>";
            return 0;
        };

        string q = "SELECT subscriptions.name, subscriptions.password FROM subscriptions JOIN emp_sub ON subscriptions.name=emp_sub.subscription WHERE emp_sub.username='" + sqlEscape(u) + "' ORDER BY subscriptions.name;";
        sqlite3_exec(DB, q.c_str(), cb, &data, NULL);

        size_t pos = html.find("{{DATA}}");
        if (pos != string::npos)
            html.replace(pos, 8, data);
        return crow::response(html); });

    CROW_ROUTE(app, "/forgot-password")([]
                                        { return crow::response(load("templates/forgot_password.html")); });

    CROW_ROUTE(app, "/forgot-password").methods("POST"_method)([](const crow::request &req)
                                                                 {
        string u = val(req.body, "username");
        if (u.empty())
            return alertAndRedirect("Enter your username.", "/forgot-password");

        if (isAdminUser(u))
            return alertAndRedirect("The admin account password cannot be reset from this page. Sign in with your admin credentials or change the password in the application source.", "/");

        if (!employeeUsernameExists(u))
            return alertAndRedirect("No employee account found with that username.", "/forgot-password");

        string gmail = smtp_gmail_user();
        string pass = smtp_gmail_app_password();
        string to = smtp_otp_to_email();
        if (to.empty())
            to = gmail;
        if (gmail.empty() || pass.empty())
            return alertAndRedirect("SMTP is not configured in the application.", "/forgot-password");

        string otp = generate_otp_6();
        string subject = "Password reset code for " + u;
        string body = "A password reset was requested for employee username: " + u + "\n\nYour reset code is: " + otp +
                      "\n\nIt expires in " + to_string(OTP_TTL_SEC / 60) + " minutes.\nIf you did not request this, ignore this email.";

        SmtpResult mail = send_gmail_smtp(gmail, pass, to, subject, body);
        if (!mail.ok)
            return alertAndRedirect("Could not send email. Check SMTP credentials in the application source and your network.", "/forgot-password");

        {
            lock_guard<mutex> lock(g_otp_mutex);
            g_forgot_otp = otp;
            g_forgot_username = u;
            g_forgot_expiry = chrono::steady_clock::now() + chrono::seconds(OTP_TTL_SEC);
            g_forgot_pending = true;
        }

        return alertAndRedirect("A reset code was sent to the admin email inbox.", "/reset-password"); });

    CROW_ROUTE(app, "/reset-password")([]
                                       { return crow::response(load("templates/reset_password.html")); });

    CROW_ROUTE(app, "/reset-password").methods("POST"_method)([](const crow::request &req)
                                                              {
        string u = val(req.body, "username");
        string otp = val(req.body, "otp");
        string p1 = val(req.body, "password");
        string p2 = val(req.body, "password2");
        if (u.empty() || otp.empty() || p1.empty())
            return alertAndRedirect("Fill in all fields.", "/reset-password");
        if (p1 != p2)
            return alertAndRedirect("Passwords do not match.", "/reset-password");

        lock_guard<mutex> lock(g_otp_mutex);
        if (!g_forgot_pending || g_forgot_username != u)
            return alertAndRedirect("No reset in progress for this username. Use Forgot password first.", "/forgot-password");

        if (chrono::steady_clock::now() > g_forgot_expiry)
        {
            g_forgot_pending = false;
            g_forgot_otp.clear();
            g_forgot_username.clear();
            return alertAndRedirect("Reset code expired. Request a new one.", "/forgot-password");
        }

        if (otp != g_forgot_otp)
            return alertAndRedirect("Invalid code.", "/reset-password");

        string q = "UPDATE employees SET password='" + sqlEscape(p1) + "' WHERE username='" + sqlEscape(u) + "';";
        sqlite3_exec(DB, q.c_str(), NULL, NULL, NULL);

        g_forgot_pending = false;
        g_forgot_otp.clear();
        g_forgot_username.clear();

        return alertAndRedirect("Password updated. You can log in now.", "/"); });

    CROW_ROUTE(app, "/login_otp").methods("POST"_method)([](const crow::request &req)
                                                         {
        string username = val(req.body, "username");
        if (username.empty())
            return alertAndRedirect("Enter username.", "/");

        // Check if employee exists
        sqlite3_stmt *stmt;
        sqlite3_prepare_v2(DB, "SELECT email FROM employees WHERE username=?;", -1, &stmt, NULL);
        sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_STATIC);
        string email;
        if (sqlite3_step(stmt) == SQLITE_ROW)
            email = (const char*)sqlite3_column_text(stmt, 0);
        sqlite3_finalize(stmt);

        if (email.empty())
            return alertAndRedirect("User not found or no email.", "/");

        // Generate OTP
        string otp = generate_otp_6();

        // Send OTP to email
        string gmail = smtp_gmail_user();
        string pass = smtp_gmail_app_password();
        SmtpResult res = send_gmail_smtp(gmail, pass, email, "Login OTP", "Your login OTP is: " + otp + "\n\nIt expires in " + to_string(OTP_TTL_SEC / 60) + " minutes.");
        if (!res.ok)
            return alertAndRedirect("Failed to send email: " + res.error_message, "/");

        // Store in globals
        lock_guard<mutex> lock(g_otp_mutex);
        g_pending_otp = otp;
        g_otp_expiry = chrono::steady_clock::now() + chrono::seconds(OTP_TTL_SEC);

        // Store username for login
        g_forgot_username = username; // reuse this variable

        // Redirect to enter OTP
        crow::response resp(302);
        resp.add_header("Location", "/enter_login_otp");
        return resp;
    });

    CROW_ROUTE(app, "/enter_login_otp").methods("GET"_method)([]
                                                              {
        string html = load("templates/enter_otp.html");
        return crow::response(200, html);
    });

    CROW_ROUTE(app, "/verify_login_otp").methods("POST"_method)([](const crow::request &req)
                                                                {
        string otp = val(req.body, "otp");

        lock_guard<mutex> lock(g_otp_mutex);
        if (g_pending_otp.empty())
            return alertAndRedirect("No OTP pending. Request a new one.", "/");

        if (chrono::steady_clock::now() > g_otp_expiry)
        {
            g_pending_otp.clear();
            return alertAndRedirect("OTP expired. Request a new one.", "/");
        }

        if (otp != g_pending_otp)
            return alertAndRedirect("Invalid OTP.", "/enter_login_otp");

        // Login user
        string username = g_forgot_username;
        g_pending_otp.clear();
        g_forgot_username.clear();

        string sid = to_string(rand());
        {
            lock_guard<mutex> lock(g_sessions_mutex);
            sessions[sid] = username;
        }
        crow::response res;
        res.code = 302;
        res.add_header("Set-Cookie", "session=" + sid + "; Path=/; HttpOnly");
        res.add_header("Location", "/employee");
        return res;
    });

    CROW_ROUTE(app, "/create_account").methods("GET"_method)([]
                                                             {
        string html = load("templates/create_account.html");
        return crow::response(200, html); });

    CROW_ROUTE(app, "/create_account").methods("POST"_method)([](const crow::request &req)
                                                               {
        crow::multipart::message msg(req);
        string username = msg.get_part_by_name("username").body;
        string password = msg.get_part_by_name("password").body;
        string email = msg.get_part_by_name("email").body;
        string role = msg.get_part_by_name("role").body;
        string weekly_holiday = msg.get_part_by_name("weekly_holiday").body;
        string photo_path;

        auto photoPart = msg.get_part_by_name("photo");
        if (!photoPart.body.empty()) {
            string filename = username + "_photo.jpg";
            auto cd = photoPart.get_header_object("Content-Disposition");
            auto it = cd.params.find("filename");
            if (it != cd.params.end() && !it->second.empty()) {
                string orig_filename = it->second;
                size_t dot_pos = orig_filename.find_last_of(".");
                if (dot_pos != string::npos)
                    filename = username + "_photo" + orig_filename.substr(dot_pos);
            }
            fs::path photo_file = fs::current_path() / "uploads" / filename;
            ofstream out(photo_file, ios::binary);
            out.write(photoPart.body.c_str(), photoPart.body.size());
            out.close();
            photo_path = "/uploads/" + filename;
        }

        if (username.empty() || password.empty() || email.empty() || role.empty() || weekly_holiday.empty() || photo_path.empty())
            return alertAndRedirect("Missing fields. Please fill all required values.", "/create_account");

        // Check if username already exists in employees or pending_accounts
        sqlite3_stmt *stmt;
        sqlite3_prepare_v2(DB, "SELECT 1 FROM employees WHERE username=?;", -1, &stmt, NULL);
        sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_STATIC);
        int rc = sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        if (rc == SQLITE_ROW)
            return alertAndRedirect("Username already exists. Please choose a different one.", "/create_account");

        sqlite3_prepare_v2(DB, "SELECT 1 FROM pending_accounts WHERE username=?;", -1, &stmt, NULL);
        sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_STATIC);
        rc = sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        if (rc == SQLITE_ROW)
            return alertAndRedirect("Account creation request already pending. Please wait for admin approval.", "/create_account");

        // Parse subscriptions
        vector<string> subscriptions;
        size_t pos = 0;
        string body = req.body;
        while ((pos = body.find("subscriptions=", pos)) != string::npos)
        {
            pos += 13;
            size_t end = body.find("&", pos);
            if (end == string::npos) end = body.size();
            string sub = body.substr(pos, end - pos);
            sub = urlDecode(sub);
            sub = normalizeSubscription(sub);
            if (!sub.empty())
                subscriptions.push_back(sub);
            pos = end;
        }

        // Store subscriptions as comma-separated
        string subs_str;
        for (size_t i = 0; i < subscriptions.size(); ++i)
        {
            if (i > 0) subs_str += ",";
            subs_str += subscriptions[i];
        }

        // Insert into pending_accounts
        sqlite3_prepare_v2(DB, "INSERT INTO pending_accounts VALUES(?, ?, ?, ?, ?, ?, ?);", -1, &stmt, NULL);
        sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, password.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 3, role.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 4, email.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 5, weekly_holiday.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 6, subs_str.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 7, photo_path.c_str(), -1, SQLITE_STATIC);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);

        return alertAndRedirect("Account creation request submitted. Wait for admin approval.", "/"); });

    CROW_ROUTE(app, "/employee_requests").methods("GET"_method)([](const crow::request &req)
                                                                {
        if (!adminOk(req))
            return crow::response(403, "Admin only");

        string html = load("templates/employee_requests.html");
        string request_rows;
        sqlite3_stmt *stmt;

        auto appendRow = [&](const string &user, const string &reqType, const string &details)
        {
            request_rows += "<tr><td>" + htmlEscape(user) + "</td><td>";
            request_rows += "<form action='/employee_requests' method='GET' style='display:inline;'>";
            request_rows += "<input type='hidden' name='reqtype' value='" + htmlEscape(reqType) + "'>";
            request_rows += details;
            request_rows += "<button type='submit'>View request</button>";
            request_rows += "</form></td></tr>";
        };

        // Pending holiday requests
        sqlite3_prepare_v2(DB, "SELECT username, date, reason FROM holiday_requests WHERE status='pending';", -1, &stmt, NULL);
        while (sqlite3_step(stmt) == SQLITE_ROW)
        {
            string user = (const char*)sqlite3_column_text(stmt, 0);
            string date = (const char*)sqlite3_column_text(stmt, 1);
            string reason = (const char*)sqlite3_column_text(stmt, 2);
            string detailInputs = "<input type='hidden' name='username' value='" + htmlEscape(user) + "'>";
            detailInputs += "<input type='hidden' name='date' value='" + htmlEscape(date) + "'>";
            detailInputs += "<input type='hidden' name='reason' value='" + htmlEscape(reason) + "'>";
            appendRow(user, "holiday", detailInputs);
        }
        sqlite3_finalize(stmt);

        // Pending weekly-holiday change requests
        sqlite3_prepare_v2(DB, "SELECT username, new_holiday FROM change_holiday_requests WHERE status='pending';", -1, &stmt, NULL);
        while (sqlite3_step(stmt) == SQLITE_ROW)
        {
            string user = (const char*)sqlite3_column_text(stmt, 0);
            string new_holiday = (const char*)sqlite3_column_text(stmt, 1);
            string detailInputs = "<input type='hidden' name='username' value='" + htmlEscape(user) + "'>";
            detailInputs += "<input type='hidden' name='new_holiday' value='" + htmlEscape(new_holiday) + "'>";
            appendRow(user, "change_holiday", detailInputs);
        }
        sqlite3_finalize(stmt);

        // Pending subscription requests
        sqlite3_prepare_v2(DB, "SELECT username, subscription FROM pending_requests WHERE status='pending';", -1, &stmt, NULL);
        while (sqlite3_step(stmt) == SQLITE_ROW)
        {
            string user = (const char*)sqlite3_column_text(stmt, 0);
            string subscription = (const char*)sqlite3_column_text(stmt, 1);
            string detailInputs = "<input type='hidden' name='username' value='" + htmlEscape(user) + "'>";
            detailInputs += "<input type='hidden' name='subscription' value='" + htmlEscape(subscription) + "'>";
            appendRow(user, "subscription", detailInputs);
        }
        sqlite3_finalize(stmt);

        // Pending account creation requests
        sqlite3_prepare_v2(DB, "SELECT username, role, email, weekly_holiday, subscriptions FROM pending_accounts;", -1, &stmt, NULL);
        while (sqlite3_step(stmt) == SQLITE_ROW)
        {
            string user = (const char*)sqlite3_column_text(stmt, 0);
            string role = (const char*)sqlite3_column_text(stmt, 1);
            string email = (const char*)sqlite3_column_text(stmt, 2);
            string weekly_holiday = (const char*)sqlite3_column_text(stmt, 3);
            string subscriptions = (const char*)sqlite3_column_text(stmt, 4);
            string detailInputs = "<input type='hidden' name='username' value='" + htmlEscape(user) + "'>";
            detailInputs += "<input type='hidden' name='role' value='" + htmlEscape(role) + "'>";
            detailInputs += "<input type='hidden' name='email' value='" + htmlEscape(email) + "'>";
            detailInputs += "<input type='hidden' name='weekly_holiday' value='" + htmlEscape(weekly_holiday) + "'>";
            detailInputs += "<input type='hidden' name='subscriptions' value='" + htmlEscape(subscriptions) + "'>";
            appendRow(user, "account", detailInputs);
        }
        sqlite3_finalize(stmt);

        if (request_rows.empty())
            request_rows = "<tr><td colspan=\"2\">No pending requests.</td></tr>";

        replacePlaceholder(html, "{{REQUEST_ROWS}}", request_rows);

        string request_detail = "<p class=\"hint\">Click View request to see the request details and respond.</p>";
        const char *reqTypePtr = req.url_params.get("reqtype");
        if (reqTypePtr)
        {
            string reqType(reqTypePtr);
            string user = req.url_params.get("username") ? string(req.url_params.get("username")) : string();
            if (reqType == "holiday")
            {
                string date = req.url_params.get("date") ? string(req.url_params.get("date")) : string();
                string reason = req.url_params.get("reason") ? string(req.url_params.get("reason")) : string();
                request_detail = "<p>" + htmlEscape(user) + " requested a holiday on " + htmlEscape(date) + " for \"" + htmlEscape(reason) + "\".</p>";
                request_detail += "<form action='/approve_holiday' method='POST' style='display:inline;'>";
                request_detail += "<input name='username' value='" + htmlEscape(user) + "' hidden>";
                request_detail += "<input name='date' value='" + htmlEscape(date) + "' hidden>";
                request_detail += "<button type='submit'>Accept</button></form> ";
                request_detail += "<form action='/reject_holiday' method='POST' style='display:inline;'>";
                request_detail += "<input name='username' value='" + htmlEscape(user) + "' hidden>";
                request_detail += "<input name='date' value='" + htmlEscape(date) + "' hidden>";
                request_detail += "<button type='submit'>Reject</button></form>";
            }
            else if (reqType == "change_holiday")
            {
                string new_holiday = req.url_params.get("new_holiday") ? string(req.url_params.get("new_holiday")) : string();
                request_detail = "<p>" + htmlEscape(user) + " requested to change weekly holiday to \"" + htmlEscape(new_holiday) + "\".</p>";
                request_detail += "<form action='/approve_change_holiday' method='POST' style='display:inline;'>";
                request_detail += "<input name='username' value='" + htmlEscape(user) + "' hidden>";
                request_detail += "<button type='submit'>Accept</button></form> ";
                request_detail += "<form action='/reject_change_holiday' method='POST' style='display:inline;'>";
                request_detail += "<input name='username' value='" + htmlEscape(user) + "' hidden>";
                request_detail += "<button type='submit'>Reject</button></form>";
            }
            else if (reqType == "subscription")
            {
                string subscription = req.url_params.get("subscription") ? string(req.url_params.get("subscription")) : string();
                request_detail = "<p>" + htmlEscape(user) + " requested subscription \"" + htmlEscape(subscription) + "\".</p>";
                request_detail += "<form action='/approve_request' method='POST' style='display:inline;'>";
                request_detail += "<input name='username' value='" + htmlEscape(user) + "' hidden>";
                request_detail += "<input name='subscription' value='" + htmlEscape(subscription) + "' hidden>";
                request_detail += "<button type='submit'>Accept</button></form> ";
                request_detail += "<form action='/reject_request' method='POST' style='display:inline;'>";
                request_detail += "<input name='username' value='" + htmlEscape(user) + "' hidden>";
                request_detail += "<input name='subscription' value='" + htmlEscape(subscription) + "' hidden>";
                request_detail += "<button type='submit'>Reject</button></form>";
            }
            else if (reqType == "account")
            {
                string role = req.url_params.get("role") ? string(req.url_params.get("role")) : string();
                string email = req.url_params.get("email") ? string(req.url_params.get("email")) : string();
                string weekly_holiday = req.url_params.get("weekly_holiday") ? string(req.url_params.get("weekly_holiday")) : string();
                string subscriptions = req.url_params.get("subscriptions") ? string(req.url_params.get("subscriptions")) : string();
                request_detail = "<p>" + htmlEscape(user) + " requested an account with role \"" + htmlEscape(role) + "\", email \"" + htmlEscape(email) + "\", weekly holiday \"" + htmlEscape(weekly_holiday) + "\" and subscriptions \"" + htmlEscape(subscriptions) + "\".</p>";
                request_detail += "<form action='/approve_account' method='POST' style='display:inline;'>";
                request_detail += "<input name='username' value='" + htmlEscape(user) + "' hidden>";
                request_detail += "<button type='submit'>Accept</button></form> ";
                request_detail += "<form action='/reject_account' method='POST' style='display:inline;'>";
                request_detail += "<input name='username' value='" + htmlEscape(user) + "' hidden>";
                request_detail += "<button type='submit'>Reject</button></form>";
            }
        }

        replacePlaceholder(html, "{{REQUEST_DETAIL}}", request_detail);
        return crow::response(200, html);
    });

    CROW_ROUTE(app, "/approve_request").methods("POST"_method)([](const crow::request &req)
                                                                {
        if (!adminOk(req))
            return crow::response(403, "Admin only");

        string username = val(req.body, "username");
        string subscription = normalizeSubscription(urlDecode(val(req.body, "subscription")));

        // Insert into emp_sub
        sqlite3_stmt *stmt;
        sqlite3_prepare_v2(DB, "INSERT INTO emp_sub VALUES(?, ?);", -1, &stmt, NULL);
        sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, subscription.c_str(), -1, SQLITE_STATIC);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);

        // Delete from pending
        sqlite3_prepare_v2(DB, "DELETE FROM pending_requests WHERE username=? AND (subscription=? OR subscription=?);", -1, &stmt, NULL);
        sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, subscription.c_str(), -1, SQLITE_STATIC);
        string subscriptionWithEquals = "=" + subscription;
        sqlite3_bind_text(stmt, 3, subscriptionWithEquals.c_str(), -1, SQLITE_STATIC);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);

        crow::response resp(302);
        resp.add_header("Location", "/employee_requests");
        return resp; });

    CROW_ROUTE(app, "/reject_request").methods("POST"_method)([](const crow::request &req)
                                                               {
        if (!adminOk(req))
            return crow::response(403, "Admin only");

        string username = val(req.body, "username");
        string subscription = normalizeSubscription(urlDecode(val(req.body, "subscription")));

        sqlite3_stmt *stmt;
        sqlite3_prepare_v2(DB, "DELETE FROM pending_requests WHERE username=? AND (subscription=? OR subscription=?);", -1, &stmt, NULL);
        sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, subscription.c_str(), -1, SQLITE_STATIC);
        string subscriptionWithEquals = "=" + subscription;
        sqlite3_bind_text(stmt, 3, subscriptionWithEquals.c_str(), -1, SQLITE_STATIC);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);

        crow::response resp(302);
        resp.add_header("Location", "/employee_requests");
        return resp; });

    CROW_ROUTE(app, "/approve_account").methods("POST"_method)([](const crow::request &req)
                                                                {
        if (!adminOk(req))
            return crow::response(403, "Admin only");

        string username = val(req.body, "username");

        // Get account details from pending_accounts
        sqlite3_stmt *stmt;
        sqlite3_prepare_v2(DB, "SELECT password, role, email, weekly_holiday, subscriptions, photo FROM pending_accounts WHERE username=?;", -1, &stmt, NULL);
        sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_STATIC);
        int rc = sqlite3_step(stmt);
        if (rc != SQLITE_ROW)
        {
            sqlite3_finalize(stmt);
            return crow::response(400, "Account not found");
        }
        string password = (const char*)sqlite3_column_text(stmt, 0);
        string role = (const char*)sqlite3_column_text(stmt, 1);
        string email = (const char*)sqlite3_column_text(stmt, 2);
        string weekly_holiday = (const char*)sqlite3_column_text(stmt, 3);
        string subs_str = (const char*)sqlite3_column_text(stmt, 4);
        string photo = (const char*)sqlite3_column_text(stmt, 5) ? (const char*)sqlite3_column_text(stmt, 5) : "";
        sqlite3_finalize(stmt);

        // Insert into employees
        sqlite3_prepare_v2(DB, "INSERT INTO employees (username, password, role, email, weekly_holiday, photo) VALUES(?, ?, ?, ?, ?, ?);", -1, &stmt, NULL);
        sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, password.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 3, role.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 4, email.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 5, weekly_holiday.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 6, photo.c_str(), -1, SQLITE_STATIC);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);

        // Insert subscriptions into emp_sub
        if (!subs_str.empty())
        {
            vector<string> subs;
            size_t pos = 0;
            while ((pos = subs_str.find(',')) != string::npos)
            {
                subs.push_back(subs_str.substr(0, pos));
                subs_str.erase(0, pos + 1);
            }
            subs.push_back(subs_str);
            for (const string &sub : subs)
            {
                if (!sub.empty())
                {
                    sqlite3_prepare_v2(DB, "INSERT INTO emp_sub VALUES(?, ?);", -1, &stmt, NULL);
                    sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_STATIC);
                    sqlite3_bind_text(stmt, 2, sub.c_str(), -1, SQLITE_STATIC);
                    sqlite3_step(stmt);
                    sqlite3_finalize(stmt);
                }
            }
        }

        // Delete from pending_accounts
        sqlite3_prepare_v2(DB, "DELETE FROM pending_accounts WHERE username=?;", -1, &stmt, NULL);
        sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_STATIC);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);

        crow::response resp(302);
        resp.add_header("Location", "/employee_requests");
        return resp; });

    CROW_ROUTE(app, "/reject_account").methods("POST"_method)([](const crow::request &req)
                                                               {
        if (!adminOk(req))
            return crow::response(403, "Admin only");

        string username = val(req.body, "username");

        sqlite3_stmt *stmt;
        sqlite3_prepare_v2(DB, "DELETE FROM pending_accounts WHERE username=?;", -1, &stmt, NULL);
        sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_STATIC);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);

        crow::response resp(302);
        resp.add_header("Location", "/employee_requests");
        return resp; });

    CROW_ROUTE(app, "/approve_holiday").methods("POST"_method)([](const crow::request &req)
                                                                {
        if (!adminOk(req))
            return crow::response(403, "Admin only");

        string username = val(req.body, "username");
        string date = val(req.body, "date");

        sqlite3_stmt *stmt;
        sqlite3_prepare_v2(DB, "UPDATE holiday_requests SET status='approved' WHERE username=? AND date=?;", -1, &stmt, NULL);
        sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, date.c_str(), -1, SQLITE_STATIC);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);

        crow::response resp(302);
        resp.add_header("Location", "/employee_requests");
        return resp; });

    CROW_ROUTE(app, "/reject_holiday").methods("POST"_method)([](const crow::request &req)
                                                               {
        if (!adminOk(req))
            return crow::response(403, "Admin only");

        string username = val(req.body, "username");
        string date = val(req.body, "date");

        sqlite3_stmt *stmt;
        sqlite3_prepare_v2(DB, "UPDATE holiday_requests SET status='rejected' WHERE username=? AND date=?;", -1, &stmt, NULL);
        sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, date.c_str(), -1, SQLITE_STATIC);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);

        crow::response resp(302);
        resp.add_header("Location", "/employee_requests");
        return resp; });

    CROW_ROUTE(app, "/approve_change_holiday").methods("POST"_method)([](const crow::request &req)
                                                                       {
        if (!adminOk(req))
            return crow::response(403, "Admin only");

        string username = val(req.body, "username");

        // Get new_holiday
        sqlite3_stmt *stmt;
        sqlite3_prepare_v2(DB, "SELECT new_holiday FROM change_holiday_requests WHERE username=? AND status='pending';", -1, &stmt, NULL);
        sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_STATIC);
        string new_holiday;
        if (sqlite3_step(stmt) == SQLITE_ROW)
            new_holiday = (const char*)sqlite3_column_text(stmt, 0);
        sqlite3_finalize(stmt);

        if (!new_holiday.empty())
        {
            // Update employee weekly_holiday
            sqlite3_prepare_v2(DB, "UPDATE employees SET weekly_holiday=? WHERE username=?;", -1, &stmt, NULL);
            sqlite3_bind_text(stmt, 1, new_holiday.c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_text(stmt, 2, username.c_str(), -1, SQLITE_STATIC);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);

            // Update status to approved
            sqlite3_prepare_v2(DB, "UPDATE change_holiday_requests SET status='approved' WHERE username=? AND status='pending';", -1, &stmt, NULL);
            sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_STATIC);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }

        crow::response resp(302);
        resp.add_header("Location", "/employee_requests");
        return resp; });

    CROW_ROUTE(app, "/reject_change_holiday").methods("POST"_method)([](const crow::request &req)
                                                                      {
        if (!adminOk(req))
            return crow::response(403, "Admin only");

        string username = val(req.body, "username");

        sqlite3_stmt *stmt;
        sqlite3_prepare_v2(DB, "UPDATE change_holiday_requests SET status='rejected' WHERE username=? AND status='pending';", -1, &stmt, NULL);
        sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_STATIC);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);

        crow::response resp(302);
        resp.add_header("Location", "/employee_requests");
        return resp; });

    CROW_ROUTE(app, "/request_holiday")([](const crow::request &req)
                                                               {
        if (!employeeOk(req))
            return redirect("/");
        return crow::response(load("templates/request_holiday.html"));
    });

    CROW_ROUTE(app, "/request_holiday").methods("POST"_method)([](const crow::request &req)
                                                                {
        if (!employeeOk(req))
            return redirect("/");
        string user = getUser(req);
        string date = val(req.body, "date");
        string reason = val(req.body, "reason");

        // Validate date format and business rules
        int reqYear, reqMonth, reqDay;
        if (!parseDate(date, reqYear, reqMonth, reqDay))
            return alertAndRedirect("Invalid date format.", "/request_holiday");

        time_t now = time(0);
        tm *ltm = localtime(&now);
        char today[11];
        sprintf(today, "%04d-%02d-%02d", 1900 + ltm->tm_year, 1 + ltm->tm_mon, ltm->tm_mday);
        if (date < string(today))
            return alertAndRedirect("Date must be today or in the future.", "/request_holiday");

        if (isDateBefore(date, 2026, 4, 6))
            return alertAndRedirect("Company did not exist before April 6 2026.", "/request_holiday");

        string weeklyHoliday = getEmployeeWeeklyHoliday(user);
        string requestDay = dayNameFromDate(date);
        if (!requestDay.empty() && requestDay == weeklyHoliday)
            return alertAndRedirect("You cannot request a holiday on your weekly holiday.", "/request_holiday");

        // Insert into holiday_requests
        sqlite3_stmt *stmt;
        sqlite3_prepare_v2(DB, "INSERT INTO holiday_requests VALUES(?, ?, ?, 'pending');", -1, &stmt, NULL);
        sqlite3_bind_text(stmt, 1, user.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, date.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 3, reason.c_str(), -1, SQLITE_STATIC);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);

        return alertAndRedirect("Holiday request submitted.", "/employee");
    });

    CROW_ROUTE(app, "/change_weekly_holiday").methods("GET"_method)([](const crow::request &req)
                                                                     {
        if (!employeeOk(req))
            return redirect("/");
        string user = getUser(req);
        string html = load("templates/change_weekly_holiday.html");

        // Get current request status
        string status = "<p>No pending request.</p>";
        sqlite3_stmt *stmt;
        sqlite3_prepare_v2(DB, "SELECT status FROM change_holiday_requests WHERE username=? ORDER BY rowid DESC LIMIT 1;", -1, &stmt, NULL);
        sqlite3_bind_text(stmt, 1, user.c_str(), -1, SQLITE_STATIC);
        if (sqlite3_step(stmt) == SQLITE_ROW)
        {
            string st = (const char*)sqlite3_column_text(stmt, 0);
            if (st == "pending")
                status = "<p>Your request is pending admin approval.</p>";
            else if (st == "approved")
                status = "<p>Your request has been approved.</p>";
            else if (st == "rejected")
                status = "<p>Your request has been rejected.</p>";
        }
        sqlite3_finalize(stmt);

        replacePlaceholder(html, "{{STATUS}}", status);
        return crow::response(html);
    });

    CROW_ROUTE(app, "/change_weekly_holiday").methods("POST"_method)([](const crow::request &req)
                                                                       {
        if (!employeeOk(req))
            return redirect("/");
        string user = getUser(req);
        string new_holiday = val(req.body, "new_holiday");

        string currentHoliday = getEmployeeWeeklyHoliday(user);
        if (new_holiday == currentHoliday)
            return alertAndRedirect("Your weekly holiday is already " + currentHoliday + ". No change request needed.", "/change_weekly_holiday");

        // Check if already has pending request
        sqlite3_stmt *stmt;
        sqlite3_prepare_v2(DB, "SELECT 1 FROM change_holiday_requests WHERE username=? AND status='pending';", -1, &stmt, NULL);
        sqlite3_bind_text(stmt, 1, user.c_str(), -1, SQLITE_STATIC);
        if (sqlite3_step(stmt) == SQLITE_ROW)
        {
            sqlite3_finalize(stmt);
            return alertAndRedirect("You already have a pending request.", "/change_weekly_holiday");
        }
        sqlite3_finalize(stmt);

        // Insert request
        sqlite3_prepare_v2(DB, "INSERT INTO change_holiday_requests VALUES(?, ?, 'pending');", -1, &stmt, NULL);
        sqlite3_bind_text(stmt, 1, user.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, new_holiday.c_str(), -1, SQLITE_STATIC);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);

        return alertAndRedirect("Change request submitted.", "/change_weekly_holiday");
    });

    CROW_ROUTE(app, "/request_subscription").methods("GET"_method)([](const crow::request &req)
                                                                   {
        string user = getUser(req);
        if (user.empty() || adminOk(req))
            return crow::response(403, "Employee only");

        string html = load("templates/request_subscription.html");

        // Build checkboxes for subscriptions the user does not already own or already requested
        string sub_checks;
        sqlite3_stmt *stmt;
        sqlite3_prepare_v2(DB, "SELECT name FROM subscriptions WHERE name NOT IN (SELECT subscription FROM emp_sub WHERE username=?) AND name NOT IN (SELECT subscription FROM pending_requests WHERE username=? AND status='pending') ORDER BY name;", -1, &stmt, NULL);
        sqlite3_bind_text(stmt, 1, user.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, user.c_str(), -1, SQLITE_STATIC);
        while (sqlite3_step(stmt) == SQLITE_ROW)
        {
            string name = (const char*)sqlite3_column_text(stmt, 0);
            // Skip invalid subscription names
            if (name.empty() || name == "}" || name.find_first_not_of(" ") == string::npos)
                continue;
            sub_checks += "<label class=\"sub-pick\"><input type=\"checkbox\" name=\"subscriptions\" value=\"" + name + "\"> " + name + "</label>";
        }
        sqlite3_finalize(stmt);

        if (sub_checks.empty())
        {
            sub_checks = "<p class=\"hint\">You already have access to all available subscriptions, or you have already requested the remaining ones.</p>";
        }

        replacePlaceholder(html, "{{SUB_CHECKS}}", sub_checks);

        return crow::response(200, html); });

    CROW_ROUTE(app, "/request_subscription").methods("POST"_method)([](const crow::request &req)
                                                                     {
        string user = getUser(req);
        if (user.empty() || adminOk(req))
            return crow::response(403, "Employee only");

        // Parse subscriptions (multiple)
        vector<string> subscriptions;
        size_t pos = 0;
        string body = req.body;
        while ((pos = body.find("subscriptions=", pos)) != string::npos)
        {
            pos += 13; // length of "subscriptions="
            size_t end = body.find("&", pos);
            if (end == string::npos) end = body.size();
            string sub = body.substr(pos, end - pos);
            sub = urlDecode(sub);
            sub = normalizeSubscription(sub);
            if (!sub.empty())
                subscriptions.push_back(sub);
            pos = end;
        }

        if (subscriptions.empty())
            return alertAndRedirect("No subscriptions selected.", "/request_subscription");

        // Insert into pending_requests
        sqlite3_stmt *stmt;
        for (const string &sub : subscriptions)
        {
            // Check if already requested or has access
            sqlite3_prepare_v2(DB, "SELECT 1 FROM emp_sub WHERE username=? AND subscription=?;", -1, &stmt, NULL);
            sqlite3_bind_text(stmt, 1, user.c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_text(stmt, 2, sub.c_str(), -1, SQLITE_STATIC);
            int rc = sqlite3_step(stmt);
            sqlite3_finalize(stmt);
            if (rc == SQLITE_ROW) continue; // already has

            sqlite3_prepare_v2(DB, "SELECT 1 FROM pending_requests WHERE username=? AND subscription=?;", -1, &stmt, NULL);
            sqlite3_bind_text(stmt, 1, user.c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_text(stmt, 2, sub.c_str(), -1, SQLITE_STATIC);
            rc = sqlite3_step(stmt);
            sqlite3_finalize(stmt);
            if (rc == SQLITE_ROW) continue; // already pending

            sqlite3_prepare_v2(DB, "INSERT INTO pending_requests VALUES(?, ?, 'pending');", -1, &stmt, NULL);
            sqlite3_bind_text(stmt, 1, user.c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_text(stmt, 2, sub.c_str(), -1, SQLITE_STATIC);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }

        return alertAndRedirect("Subscription request submitted", "/employee"); });

    app.port(18080).multithreaded().run();
    return 0;
}
