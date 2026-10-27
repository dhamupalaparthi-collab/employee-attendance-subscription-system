# OOP Concepts in Employee Attendance & Subscription System

## Overview

This document explains the Object-Oriented Programming (OOP) concepts implemented in the Employee Management System. The code has been refactored to demonstrate abstraction, inheritance, polymorphism, and encapsulation.

---

## Files Structure

### 1. `oops_concepts.h`
- **Header file** containing all class definitions
- Demonstrates OOP concepts with real-world domain models
- Can be included in any C++ project

### 2. `oops_demo.cpp`
- **Demonstration program** showing how to use the OOP classes
- Compile and run separately to see OOP concepts in action
- Useful for learning and teaching OOP

### 3. `main.cpp`
- **Main application** includes `oops_concepts.h`
- Uses the OOP classes in the web application

---

## OOP Concepts Explained

### 1. ABSTRACTION
**Definition**: Abstraction hides complex implementation details and shows only the necessary features.

**Implementation in code**:
```cpp
class User {
    virtual ~User() {}
    virtual bool canViewAttendance() const = 0;  // Pure virtual function
    virtual bool canManageEmployees() const = 0;
    virtual void displayRole() const = 0;
};
```

**Example**: The `User` class is abstract. We cannot create `User user("test", "password");` directly. Instead, we create `Admin` or `Employee` objects.

**Files**: `oops_concepts.h` lines 10-47

---

### 2. INHERITANCE
**Definition**: Inheritance allows a derived class to inherit properties and methods from a base class.

**Implementation in code**:
```cpp
class Admin : public User {
    // Inherits username, password, role from User
    // Inherits getUsername(), getRole() methods
    // Must implement pure virtual functions
};

class Employee : public User {
    // Same inheritance structure
    // Adds additional members: department, employeeID
};
```

**Hierarchy**:
```
        User (Abstract Base Class)
       /    \
    Admin   Employee
```

**Other Hierarchies**:
- `AttendanceRecord` → `DailyAttendance`, `MonthlyAttendanceSummary`
- `Subscription` → `BasicSubscription`, `PremiumSubscription`, `EnterpriseSubscription`
- `Request` → `SubscriptionRequest`

**Files**: `oops_concepts.h` lines 49-130, 210-310

---

### 3. POLYMORPHISM (Method Overriding)
**Definition**: Polymorphism allows derived classes to override base class methods with specialized implementations.

**Implementation in code**:

**Example 1: User permissions**
```cpp
class Admin : public User {
    bool canViewAttendance() const override { return true; }
    bool canManageEmployees() const override { return true; }
    bool canMarkAttendance() const override { return false; }
};

class Employee : public User {
    bool canViewAttendance() const override { return true; }
    bool canManageEmployees() const override { return false; }
    bool canMarkAttendance() const override { return true; }
};
```

**Example 2: Attendance reports**
```cpp
class DailyAttendance : public AttendanceRecord {
    string generateReport() const override {
        // Daily specific report
    }
};

class MonthlyAttendanceSummary : public AttendanceRecord {
    string generateReport() const override {
        // Monthly summary report
    }
};
```

**Example 3: Subscription details**
```cpp
class BasicSubscription : public Subscription {
    string getDetails() const override { /* Basic details */ }
};

class PremiumSubscription : public Subscription {
    string getDetails() const override { /* Premium details */ }
};

class EnterpriseSubscription : public Subscription {
    string getDetails() const override { /* Enterprise details */ }
};
```

**Files**: `oops_concepts.h` 

---

### 4. ENCAPSULATION
**Definition**: Encapsulation bundles data and methods together while hiding internal details using access modifiers (private, protected, public).

**Implementation in code**:
```cpp
class Employee : public User {
private:
    string department;      // Hidden from outside
    int employeeID;         // Hidden from outside

public:
    string getDepartment() const { return department; }  // Controlled access
    int getEmployeeID() const { return employeeID; }     // Controlled access
};
```

**Benefits**:
- Password is never directly accessible - only set during construction
- Subscription cost is protected - only read through public method
- Department information is only accessible through public getters

**Files**: `oops_concepts.h` - throughout all classes

---

### 5. VIRTUAL DESTRUCTOR
**Definition**: Virtual destructors ensure proper cleanup of derived class objects when deleted through base class pointers.

**Implementation in code**:
```cpp
class User {
    virtual ~User() {}  // Virtual destructor
};

// Proper cleanup
User *admin = new Admin("dhamu", "pass");
delete admin;  // Calls Admin's destructor properly, not just User's
```

**Files**: `oops_concepts.h` lines 18, 145, 175, 208

---

## Class Hierarchy Diagram

```
┌─────────────────────────────────────────────────────────────┐
│                      User (Abstract)                        │
│  ├─ Permissions: canViewAttendance()                        │
│  ├─ Permissions: canManageEmployees()                       │
│  ├─ Permissions: canMarkAttendance()                        │
│  └─ Display: displayRole()                                  │
└─────────────────────────────────────────────────────────────┘
         │                                              │
   ┌─────▼─────┐                               ┌───────▼───────┐
   │   Admin   │                               │   Employee    │
   │           │                               │               │
   │ Manage:  │                               │ Can:          │
   │ - Employees                              │ - Mark Attend |
   │ - Subscriptions                          │ - Request Sub |
   │ - Requests                               │ - View Attend |
   └───────────┘                               └───────────────┘

┌──────────────────────────────────────────────────────────────────┐
│              AttendanceRecord (Abstract)                         │
│  ├─ generateReport()                                             │
└──────────────────────────────────────────────────────────────────┘
         │                                              │
   ┌─────▼──────────┐                         ┌────────▼──────┐
   │ DailyAttend    │                         │ MonthlySummary│
   │ + workingHours │                         │ + presentDays │
   │ + generateRpt()│                         │ + generateRpt()│
   └────────────────┘                         └────────────────┘

┌──────────────────────────────────────────────────────────────────┐
│                  Subscription (Abstract)                         │
│  ├─ subscriptionName                                             │
│  ├─ getDetails()                                                 │
└──────────────────────────────────────────────────────────────────┘
      │                      │                         │
  ┌───▼────┐          ┌──────▼─────┐        ┌─────────▼─────────┐
  │ Basic  │          │ Premium    │        │ Enterprise        │
  │$9.99   │          │ $99.99     │        │ $499.99           │
  │+ Ads   │          │+ Support   │        │+ API, SSO, Train  │
  └────────┘          │+ Analytics │        │+ Unlimited Users  │
                      └────────────┘        └───────────────────┘

┌──────────────────────────────────────────────────────────────────┐
│                    Request (Abstract)                            │
│  ├─ requesterUsername                                            │
│  ├─ status (pending/approved/rejected)                           │
│  ├─ getRequestDetails()                                          │
└──────────────────────────────────────────────────────────────────┘
         │
    ┌────▼────────────────┐
    │SubscriptionRequest   │
    │ + requestedSub       │
    │ + requestDate        │
    │ + getRequestDetails()│
    └─────────────────────┘
```

---

## How to Use

### Run the Demo Program
```bash
cd c:\Users\dhamodharam\Desktop\appnew
g++ -o oops_demo oops_demo.cpp
./oops_demo
```

This will show you:
- How abstraction works
- How inheritance works
- How polymorphism works
- How encapsulation works
- How virtual destructors work

### Use in Main Application
The `main.cpp` already includes `oops_concepts.h`:
```cpp
#include "oops_concepts.h"
```

You can now use the classes:
```cpp
Admin admin("dhamu", "password");
Employee emp("john_doe", "pass", "IT", 101);

emp.markAttendance();
emp.requestSubscription("Premium Plan");
```

---

## Key Takeaways

| Concept | Purpose | Example in Code |
|---------|---------|-----------------|
| **Abstraction** | Hide complexity | `User` abstract class |
| **Inheritance** | Reuse code | `Admin` inherits from `User` |
| **Polymorphism** | Override methods | `canMarkAttendance()` in `Admin` vs `Employee` |
| **Encapsulation** | Protect data | Private `department`, public `getDepartment()` |
| **Virtual Destructor** | Proper cleanup | `virtual ~User()` ensures derived cleanup |

---

## Benefits of This Design

1. **Maintainability**: Easy to modify individual classes without affecting others
2. **Extensibility**: Can easily add new user types (Manager, Supervisor) by inheriting from User
3. **Reusability**: Classes can be used in different projects
4. **Type Safety**: C++ compiler checks type compatibility
5. **Real-world Modeling**: Classes represent real entities (Admin, Employee, Subscriptions)
6. **Polymorphic Behavior**: Process different types uniformly through base class pointers/references

---

## Files Reference

| File | Purpose | Lines |
|------|---------|-------|
| `oops_concepts.h` | Class definitions | All OOP implementations |
| `oops_demo.cpp` | Learning/Reference | Demonstrations of each concept |
| `main.cpp` | Application | Includes oops_concepts.h |

---

## Questions for Discussion

1. Why do we use abstract base classes?
2. How does inheritance promote code reuse?
3. What is the benefit of polymorphism?
4. Why is virtual destructor important?
5. How does encapsulation protect data integrity?
6. Can a class inherit from multiple classes? (Answer: Yes, in C++ - multiple inheritance)

---

**Author's Note**: This design demonstrates professional-grade OOP practices used in enterprise software development. Understanding these concepts is crucial for becoming an experienced C++ developer.
