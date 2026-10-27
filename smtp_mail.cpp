#include "smtp_mail.h"

#include <curl/curl.h>

#include <atomic>
#include <cstring>
#include <sstream>

namespace
{

std::atomic<bool> g_curl_global_done{false};

struct UploadCtx
{
    std::string payload;
    size_t offset{0};
};

static size_t read_callback(char *ptr, size_t size, size_t nmemb, void *userdata)
{
    auto *ctx = static_cast<UploadCtx *>(userdata);
    const size_t buffer_size = size * nmemb;
    if (ctx->offset >= ctx->payload.size())
        return 0;

    const size_t remain = ctx->payload.size() - ctx->offset;
    const size_t copylen = remain < buffer_size ? remain : buffer_size;
    std::memcpy(ptr, ctx->payload.data() + ctx->offset, copylen);
    ctx->offset += copylen;
    return copylen;
}

static std::string build_rfc822(const std::string &from, const std::string &to,
                                  const std::string &subject, const std::string &body_text)
{
    std::ostringstream oss;
    oss << "From: " << from << "\r\n";
    oss << "To: " << to << "\r\n";
    oss << "Subject: " << subject << "\r\n";
    oss << "MIME-Version: 1.0\r\n";
    oss << "Content-Type: text/plain; charset=UTF-8\r\n";
    oss << "\r\n";
    oss << body_text;
    return oss.str();
}

} // namespace

void curl_smtp_global_init()
{
    if (!g_curl_global_done.exchange(true))
        curl_global_init(CURL_GLOBAL_DEFAULT);
}

void curl_smtp_global_cleanup()
{
    if (g_curl_global_done.exchange(false))
        curl_global_cleanup();
}

SmtpResult send_gmail_smtp(const std::string &gmail_address,
                           const std::string &app_password,
                           const std::string &to_address,
                           const std::string &subject,
                           const std::string &body)
{
    SmtpResult out;

    if (gmail_address.empty() || app_password.empty())
    {
        out.error_message = "Gmail address or app password is empty.";
        return out;
    }
    if (to_address.empty())
    {
        out.error_message = "Recipient address is empty.";
        return out;
    }

    CURL *curl = curl_easy_init();
    if (!curl)
    {
        out.error_message = "curl_easy_init() failed.";
        return out;
    }

    UploadCtx upload;
    upload.payload = build_rfc822(gmail_address, to_address, subject, body);

    struct curl_slist *recipients = nullptr;
    recipients = curl_slist_append(recipients, to_address.c_str());

    curl_easy_setopt(curl, CURLOPT_URL, "smtp://smtp.gmail.com:587");
    curl_easy_setopt(curl, CURLOPT_USE_SSL, static_cast<long>(CURLUSESSL_ALL));
    curl_easy_setopt(curl, CURLOPT_USERNAME, gmail_address.c_str());
    curl_easy_setopt(curl, CURLOPT_PASSWORD, app_password.c_str());

    curl_easy_setopt(curl, CURLOPT_MAIL_FROM, gmail_address.c_str());
    curl_easy_setopt(curl, CURLOPT_MAIL_RCPT, recipients);

    curl_easy_setopt(curl, CURLOPT_READFUNCTION, read_callback);
    curl_easy_setopt(curl, CURLOPT_READDATA, &upload);
    curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L);
    curl_easy_setopt(curl, CURLOPT_INFILESIZE_LARGE, static_cast<curl_off_t>(upload.payload.size()));

    /* Timeouts help surface network / TLS issues instead of hanging forever */
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 20L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60L);

    const CURLcode rc = curl_easy_perform(curl);

    curl_slist_free_all(recipients);
    curl_easy_cleanup(curl);

    if (rc != CURLE_OK)
    {
        out.error_message = std::string("SMTP failed: ") + curl_easy_strerror(rc);
#if LIBCURL_VERSION_NUM >= 0x074200
        if (rc == CURLE_LOGIN_DENIED)
            out.error_message += " (check Gmail address, App Password, and 2-Step Verification).";
#endif
        return out;
    }

    out.ok = true;
    return out;
}
