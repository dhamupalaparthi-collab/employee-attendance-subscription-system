#ifndef OOPS_CONCEPTS_H
#define OOPS_CONCEPTS_H

#include <string>
#include <vector>
#include <ctime>
#include <iostream>

using namespace std;

// ============================================================================
// ABSTRACTION: Abstract base class defining the interface for all users
// ============================================================================
class User
{
public:
    string username;
    string password;
    string role;

    // Default constructor
    User() : username(""), password(""), role("") {}

    // Constructor with parameters
    User(const string &u, const string &p, const string &r) 
        : username(u), password(p), role(r) {}

    // Virtual destructor for proper cleanup in derived classes
    virtual ~User() {}

    // Pure virtual function - ABSTRACTION
    virtual bool canViewAttendance() const = 0;
    virtual bool canManageEmployees() const = 0;
    virtual bool canMarkAttendance() const = 0;
    virtual void displayRole() const = 0;

    // Concrete method available to all users
    string getUsername() const
    {
        return username;
    }

    string getRole() const
    {
        return role;
    }
};

// ============================================================================
// INHERITANCE: Admin class inherits from User
// ============================================================================
class Admin : public User
{
public:
    Admin() : User("admin", "configured-locally", "admin") {}

    Admin(const string &uname, const string &pwd) 
        : User(uname, pwd, "admin") {}

    // POLYMORPHISM (OVERRIDING): Override base class pure virtual functions
    bool canViewAttendance() const override
    {
        return true; // Admin can view attendance
    }

    bool canManageEmployees() const override
    {
        return true; // Admin can manage employees
    }

    bool canMarkAttendance() const override
    {
        return false; // Admin cannot mark attendance
    }

    void displayRole() const override
    {
        cout << "User: " << username << " | Role: ADMINISTRATOR" << endl;
        cout << "Permissions: Manage Employees, Subscriptions, View Attendance" << endl;
    }

    // Admin-specific method
    void approveSubscriptionRequest(const string &empUsername, const string &subName)
    {
        cout << "Admin " << username << " approved subscription '"
             << subName << "' for employee '" << empUsername << "'" << endl;
    }
};

// ============================================================================
// INHERITANCE: Employee class inherits from User
// ============================================================================
class Employee : public User
{
private:
    string department;
    int employeeID;

public:
    Employee() : User("", "", "employee"), department(""), employeeID(0) {}

    Employee(const string &uname, const string &pwd, const string &dept, int id)
        : User(uname, pwd, "employee"), department(dept), employeeID(id) {}

    // POLYMORPHISM (OVERRIDING): Override base class pure virtual functions
    bool canViewAttendance() const override
    {
        return true; // Employee can view their own attendance
    }

    bool canManageEmployees() const override
    {
        return false; // Employee cannot manage other employees
    }

    bool canMarkAttendance() const override
    {
        return true; // Employee can mark their attendance
    }

    void displayRole() const override
    {
        cout << "User: " << username << " | Role: EMPLOYEE" << endl;
        cout << "Employee ID: " << employeeID << " | Department: " << department << endl;
        cout << "Permissions: Mark Attendance, View Own Attendance, Request Subscriptions" << endl;
    }

    // Employee-specific methods
    string getDepartment() const
    {
        return department;
    }

    int getEmployeeID() const
    {
        return employeeID;
    }

    void requestSubscription(const string &subName)
    {
        cout << "Employee " << username << " has requested access to subscription: " << subName << endl;
    }

    void markAttendance()
    {
        time_t now = time(0);
        tm *ltm = localtime(&now);
        cout << "Employee " << username << " marked attendance on "
             << (1900 + ltm->tm_year) << "-" << (1 + ltm->tm_mon) << "-" << ltm->tm_mday << endl;
    }
};

// ============================================================================
// ABSTRACTION: Abstract class for attendance management
// ============================================================================
class AttendanceRecord
{
protected:
    string employeeUsername;
    string attendanceDate;
    bool isPresent;

public:
    AttendanceRecord(const string &empName, const string &date)
        : employeeUsername(empName), attendanceDate(date), isPresent(true) {}

    virtual ~AttendanceRecord() {}

    // Pure virtual function - ABSTRACTION
    virtual string generateReport() const = 0;

    // Concrete methods
    string getEmployeeUsername() const
    {
        return employeeUsername;
    }

    string getDate() const
    {
        return attendanceDate;
    }

    bool getStatus() const
    {
        return isPresent;
    }
};

// ============================================================================
// INHERITANCE: Daily attendance record implementation
// ============================================================================
class DailyAttendance : public AttendanceRecord
{
private:
    int workingHours;

public:
    DailyAttendance(const string &empName, const string &date, int hours)
        : AttendanceRecord(empName, date), workingHours(hours) {}

    // POLYMORPHISM (OVERRIDING): Override pure virtual function
    string generateReport() const override
    {
        string report = "Daily Attendance Report\n";
        report += "Employee: " + employeeUsername + "\n";
        report += "Date: " + attendanceDate + "\n";
        report += "Status: " + (isPresent ? string("PRESENT") : string("ABSENT")) + "\n";
        report += "Working Hours: " + to_string(workingHours) + "\n";
        return report;
    }

    int getWorkingHours() const
    {
        return workingHours;
    }
};

// ============================================================================
// INHERITANCE: Monthly attendance summary
// ============================================================================
class MonthlyAttendanceSummary : public AttendanceRecord
{
private:
    int presentDays;
    int absentDays;

public:
    MonthlyAttendanceSummary(const string &empName, const string &monthYear, int present, int absent)
        : AttendanceRecord(empName, monthYear), presentDays(present), absentDays(absent) {}

    // POLYMORPHISM (OVERRIDING): Override pure virtual function
    string generateReport() const override
    {
        string report = "Monthly Attendance Summary\n";
        report += "Employee: " + employeeUsername + "\n";
        report += "Month: " + attendanceDate + "\n";
        report += "Present Days: " + to_string(presentDays) + "\n";
        report += "Absent Days: " + to_string(absentDays) + "\n";
        float attendance_percentage = (float)presentDays / (presentDays + absentDays) * 100;
        report += "Attendance %: " + to_string(attendance_percentage) + "%\n";
        return report;
    }

    int getPresentDays() const
    {
        return presentDays;
    }

    int getAbsentDays() const
    {
        return absentDays;
    }
};

// ============================================================================
// ABSTRACTION: Abstract base class for subscription services
// ============================================================================
class Subscription
{
protected:
    string subscriptionName;
    string password;
    float cost;

public:
    Subscription(const string &name, const string &pwd, float c)
        : subscriptionName(name), password(pwd), cost(c) {}

    virtual ~Subscription() {}

    // Pure virtual function - ABSTRACTION
    virtual string getDetails() const = 0;

    // Concrete methods
    string getName() const
    {
        return subscriptionName;
    }

    float getCost() const
    {
        return cost;
    }

    virtual bool isActive() const
    {
        return true;
    }
};

// ============================================================================
// INHERITANCE: Premium subscription type
// ============================================================================
class PremiumSubscription : public Subscription
{
private:
    vector<string> features;

public:
    PremiumSubscription(const string &name, const string &pwd)
        : Subscription(name, pwd, 99.99)
    {
        features = {"High Priority Support", "Advanced Analytics", "Custom Reports", "Dedicated Account Manager"};
    }

    // POLYMORPHISM (OVERRIDING): Override pure virtual function
    string getDetails() const override
    {
        string details = "=== PREMIUM SUBSCRIPTION ===\n";
        details += "Name: " + subscriptionName + "\n";
        details += "Cost: $" + to_string(cost) + "/month\n";
        details += "Features:\n";
        for (const string &feature : features)
        {
            details += "  - " + feature + "\n";
        }
        return details;
    }

    void addFeature(const string &feature)
    {
        features.push_back(feature);
    }
};

// ============================================================================
// INHERITANCE: Basic subscription type
// ============================================================================
class BasicSubscription : public Subscription
{
private:
    vector<string> features;
    bool adsEnabled;

public:
    BasicSubscription(const string &name, const string &pwd)
        : Subscription(name, pwd, 9.99), adsEnabled(true)
    {
        features = {"Standard Support", "Basic Analytics"};
    }

    // POLYMORPHISM (OVERRIDING): Override pure virtual function
    string getDetails() const override
    {
        string details = "=== BASIC SUBSCRIPTION ===\n";
        details += "Name: " + subscriptionName + "\n";
        details += "Cost: $" + to_string(cost) + "/month\n";
        details += "Features:\n";
        for (const string &feature : features)
        {
            details += "  - " + feature + "\n";
        }
        details += "Ads: " + (adsEnabled ? string("ENABLED") : string("DISABLED")) + "\n";
        return details;
    }

    bool hasAds() const
    {
        return adsEnabled;
    }
};

// ============================================================================
// INHERITANCE: Enterprise subscription type
// ============================================================================
class EnterpriseSubscription : public Subscription
{
private:
    int maxUsers;
    vector<string> features;

public:
    EnterpriseSubscription(const string &name, const string &pwd)
        : Subscription(name, pwd, 499.99), maxUsers(unlimited)
    {
        features = {"24/7 Premium Support", "Advanced Analytics", "Custom Reports", "Dedicated Account Manager",
                    "API Access", "SSO Integration", "Custom Training"};
    }

    // POLYMORPHISM (OVERRIDING): Override pure virtual function
    string getDetails() const override
    {
        string details = "=== ENTERPRISE SUBSCRIPTION ===\n";
        details += "Name: " + subscriptionName + "\n";
        details += "Cost: $" + to_string(cost) + "/month\n";
        details += "Max Users: UNLIMITED\n";
        details += "Features:\n";
        for (const string &feature : features)
        {
            details += "  - " + feature + "\n";
        }
        return details;
    }

    int getMaxUsers() const
    {
        return maxUsers;
    }

private:
    static const int unlimited = -1;
};

// ============================================================================
// ABSTRACTION: Request management interface
// ============================================================================
class Request
{
protected:
    string requesterUsername;
    string status; // pending, approved, rejected

public:
    Request(const string &requester) : requesterUsername(requester), status("pending") {}

    virtual ~Request() {}

    // Pure virtual function - ABSTRACTION
    virtual string getRequestDetails() const = 0;

    // Concrete methods
    string getStatus() const
    {
        return status;
    }

    void approve()
    {
        status = "approved";
        cout << "Request from " << requesterUsername << " has been APPROVED" << endl;
    }

    void reject()
    {
        status = "rejected";
        cout << "Request from " << requesterUsername << " has been REJECTED" << endl;
    }

    string getRequester() const
    {
        return requesterUsername;
    }
};

// ============================================================================
// INHERITANCE: Subscription request implementation
// ============================================================================
class SubscriptionRequest : public Request
{
private:
    string requestedSubscription;
    string requestDate;

public:
    SubscriptionRequest(const string &requester, const string &subscription, const string &date)
        : Request(requester), requestedSubscription(subscription), requestDate(date) {}

    // POLYMORPHISM (OVERRIDING): Override pure virtual function
    string getRequestDetails() const override
    {
        string details = "Subscription Request Details\n";
        details += "Requested by: " + requesterUsername + "\n";
        details += "Subscription: " + requestedSubscription + "\n";
        details += "Request Date: " + requestDate + "\n";
        details += "Status: " + status + "\n";
        return details;
    }

    string getRequestedSubscription() const
    {
        return requestedSubscription;
    }
};

// ============================================================================
// ABSTRACTION: Data Access Object (DAO) Pattern
// ============================================================================
class UserRepository
{
public:
    virtual ~UserRepository() {}

    // Pure virtual functions - ABSTRACTION for database operations
    virtual bool addUser(const User &user) = 0;
    virtual User *getUser(const string &username) = 0;
    virtual bool updateUser(const User &user) = 0;
    virtual bool deleteUser(const string &username) = 0;
    virtual vector<User *> getAllUsers() = 0;
};

// ============================================================================
// EXPLANATION OF OOP CONCEPTS USED:
// ============================================================================
/*
 * 1. ENCAPSULATION:
 *    - Private members (password, cost, etc.) are hidden from outside access
 *    - Public methods provide controlled access to data
 *    - Example: Employee class with private department and employeeID
 *
 * 2. ABSTRACTION:
 *    - User, AttendanceRecord, Subscription, Request are abstract base classes
 *    - They define interfaces without implementation details
 *    - Pure virtual functions (= 0) force derived classes to implement them
 *    - Example: User::canMarkAttendance() is an abstract interface
 *
 * 3. INHERITANCE:
 *    - Admin and Employee inherit from User
 *    - DailyAttendance and MonthlyAttendanceSummary inherit from AttendanceRecord
 *    - PremiumSubscription, BasicSubscription, EnterpriseSubscription inherit from Subscription
 *    - SubscriptionRequest inherits from Request
 *    - Example: class Admin : public User { ... }
 *
 * 4. POLYMORPHISM (METHOD OVERRIDING):
 *    - Derived classes override pure virtual functions from base class
 *    - Same function name, different implementation in each derived class
 *    - Example: canViewAttendance() returns true for Admin, true for Employee
 *    - Example: generateReport() has different implementations in DailyAttendance vs MonthlyAttendanceSummary
 *    - Virtual functions allow runtime polymorphism
 *
 * 5. VIRTUAL DESTRUCTOR:
 *    - Ensures proper cleanup when deleting derived class objects through base class pointers
 *    - Example: virtual ~User() {}
 *
 * 6. DATA MEMBERS PROTECTION:
 *    - Private: Only accessible within the class
 *    - Protected: Accessible in derived classes
 *    - Public: Accessible from outside
 */

#endif
