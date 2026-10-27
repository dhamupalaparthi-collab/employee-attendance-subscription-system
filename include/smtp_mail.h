#pragma once

#include <string>

struct SmtpResult
{
    bool ok{false};
    std::string error_message;
};

/// Send a plain-text email via Gmail SMTP (smtp.gmail.com:587, STARTTLS).
/// Uses CURLOPT_READFUNCTION (no POSTFIELDS).
/// \param gmail_address Full Gmail address used to authenticate (and as From).
/// \param app_password    Gmail App Password (not your normal login password).
/// \param to_address      Recipient (e.g. same as admin inbox).
/// \param subject         Email subject line.
/// \param body            Plain-text body (e.g. OTP message).
SmtpResult send_gmail_smtp(const std::string &gmail_address,
                           const std::string &app_password,
                           const std::string &to_address,
                           const std::string &subject,
                           const std::string &body);

/// Call once before any send (safe to call multiple times).
void curl_smtp_global_init();

/// Optional cleanup on shutdown.
void curl_smtp_global_cleanup();
