# Employee Attendance & Subscription Management System

A C++17 web application for managing employees, attendance, subscriptions, holiday requests, and administrative approvals.

## Features

### Administrators

- Sign in through the admin dashboard
- Add, edit, and remove employee accounts
- Approve or reject new employee registrations
- Add, edit, and remove subscriptions
- Approve or reject employee subscription requests
- View employee attendance calendars
- Approve or reject holiday requests
- Approve or reject weekly-holiday change requests
- Change employee passwords

### Employees

- Create an account with email and profile photo
- Sign in with OTP verification
- View a personal dashboard
- Mark daily attendance
- View personal attendance history
- View assigned subscriptions and credentials
- Request subscriptions
- Request holidays
- Request a weekly holiday change
- Change a profile photo
- Reset a forgotten password using an email OTP

## Technology

- C++17
- [Crow](https://github.com/CrowCpp/Crow) HTTP framework
- SQLite
- libcurl and Gmail SMTP for OTP email
- HTML/CSS server-rendered templates
- CMake

## Project structure

| Path | Purpose |
|---|---|
| `main.cpp` | Crow web server, routes, authentication, database operations, and application logic |
| `smtp_mail.cpp` / `include/smtp_mail.h` | Gmail SMTP integration through libcurl |
| `sqlite3.c` / `sqlite3.h` | SQLite amalgamation used by the application |
| `oops_concepts.h` | OOP domain classes and demonstrations of abstraction, inheritance, polymorphism, and encapsulation |
| `oops_demo.cpp` | Standalone OOP demonstration program |
| `templates/` | HTML pages rendered by the server |
| `static/style.css` | Shared frontend styles |
| `CMakeLists.txt` | CMake build configuration |
| `company.db` | Local runtime database; intentionally not tracked |
| `uploads/` | Local employee photos; intentionally not tracked |

## Prerequisites

- Windows
- CMake 3.10 or newer
- A C++17-compatible compiler
- libcurl development files
- OpenSSL development files
- MSYS2/MinGW is recommended for the current CMake configuration

For MSYS2 MinGW64, install the required packages:

```bash
pacman -S mingw-w64-x86_64-curl mingw-w64-x86_64-openssl mingw-w64-x86_64-cmake
```

## Build

From the project directory:

```bash
cmake -G "MinGW Makefiles" -S . -B build -DCMAKE_PREFIX_PATH=C:/msys64/mingw64
cmake --build build
```

The executable is produced as `build/app.exe`.

## Configuration

Sensitive values are read from environment variables:

| Variable | Purpose |
|---|---|
| `APP_ADMIN_USERNAME` | Administrator username |
| `APP_ADMIN_PASSWORD` | Administrator password |
| `SMTP_GMAIL_ADDRESS` | Gmail address used to send OTP emails |
| `SMTP_GMAIL_APP_PASSWORD` | Gmail app password |
| `SMTP_RECIPIENT_EMAIL` | Email address that receives OTP messages |

Example in PowerShell:

```powershell
$env:APP_ADMIN_USERNAME = "admin"
$env:APP_ADMIN_PASSWORD = "use-a-strong-local-password"
$env:SMTP_GMAIL_ADDRESS = "sender@example.com"
$env:SMTP_GMAIL_APP_PASSWORD = "your-gmail-app-password"
$env:SMTP_RECIPIENT_EMAIL = "recipient@example.com"
.\build\app.exe
```

Do not commit real passwords, app passwords, API keys, employee data, database files, or uploaded photos.

## Run

Start the application and open:

```text
http://localhost:18080
```

The application creates or upgrades `company.db` and creates the `uploads/` directory when it starts.

## OOP demonstration

Run the standalone demonstration after compiling it with a C++17 compiler:

```bash
g++ -std=c++17 -o oops_demo oops_demo.cpp
./oops_demo
```

The OOP model includes:

```text
User
├── Admin
└── Employee

AttendanceRecord
├── DailyAttendance
└── MonthlyAttendanceSummary

Subscription
├── BasicSubscription
├── PremiumSubscription
└── EnterpriseSubscription

Request
└── SubscriptionRequest
```

See [OOPS_README.md](OOPS_README.md) for a detailed explanation.

## Security notes

This project is intended for local development and educational use. Before deploying it to a real environment, add password hashing, prepared statements for every query, CSRF protection, secure session storage, secure cookie attributes, upload validation, rate limiting, and production-grade error handling.

## License

No license has been selected yet. Add a license before distributing the project.
