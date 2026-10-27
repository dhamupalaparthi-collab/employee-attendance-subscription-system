/*
 * ============================================================================
 * OOPS CONCEPTS DEMONSTRATION
 * ============================================================================
 * 
 * This file demonstrates the OOP concepts used in the Employee Attendance
 * and Subscription Management System.
 *
 * To compile and run this demo:
 *   g++ -o oops_demo oops_demo.cpp
 *   ./oops_demo
 *
 * ============================================================================
 */

#include "oops_concepts.h"
#include <iostream>

using namespace std;

// ============================================================================
// DEMONSTRATION OF OOP CONCEPTS
// ============================================================================

void demonstrateAbstraction()
{
    cout << "\n========== ABSTRACTION DEMONSTRATION ==========\n";
    cout << "Abstraction hides implementation details and shows only functionality.\n";
    cout << "We cannot create an object of abstract class 'User' directly.\n";
    cout << "But we can create objects of derived classes (Admin, Employee).\n";
    
    // The following would cause a compilation error:
    // User user("test", "password"); // ERROR: Cannot instantiate abstract class
    
    // Instead, we create derived class objects:
    Admin admin("dhamu", "admin123");
    Employee employee("john_doe", "pass456", "IT", 101);
    
    cout << "\nCreated objects of derived classes:\n";
    admin.displayRole();
    cout << "\n";
    employee.displayRole();
}

void demonstrateInheritance()
{
    cout << "\n========== INHERITANCE DEMONSTRATION ==========\n";
    cout << "Inheritance allows derived classes to inherit properties and methods from base class.\n";
    cout << "Admin and Employee classes inherit from User class.\n\n";

    Admin admin("dhamu", "admin123");
    cout << "Admin inherited getUsername() from User: " << admin.getUsername() << endl;
    cout << "Admin inherited getRole() from User: " << admin.getRole() << endl;

    Employee employee("john_doe", "pass456", "IT", 101);
    cout << "\nEmployee inherited getUsername() from User: " << employee.getUsername() << endl;
    cout << "Employee inherited getRole() from User: " << employee.getRole() << endl;
    cout << "Employee has own methods: getDepartment() = " << employee.getDepartment() << endl;
}

void demonstratePolymorphism()
{
    cout << "\n========== POLYMORPHISM (METHOD OVERRIDING) DEMONSTRATION ==========\n";
    cout << "Polymorphism allows derived classes to override base class methods.\n";
    cout << "Same method name, different behavior in each class.\n\n";

    Admin admin("dhamu", "admin123");
    Employee employee("john_doe", "pass456", "IT", 101);

    cout << "Admin canViewAttendance(): " << (admin.canViewAttendance() ? "YES" : "NO") << endl;
    cout << "Employee canViewAttendance(): " << (employee.canViewAttendance() ? "YES" : "NO") << endl << endl;

    cout << "Admin canMarkAttendance(): " << (admin.canMarkAttendance() ? "YES" : "NO") << endl;
    cout << "Employee canMarkAttendance(): " << (employee.canMarkAttendance() ? "YES" : "NO") << endl << endl;

    cout << "Admin canManageEmployees(): " << (admin.canManageEmployees() ? "YES" : "NO") << endl;
    cout << "Employee canManageEmployees(): " << (employee.canManageEmployees() ? "YES" : "NO") << endl;
}

void demonstrateAttendancePolymorphism()
{
    cout << "\n========== ATTENDANCE RECORD POLYMORPHISM ==========\n";
    cout << "Different types of attendance records override generateReport() differently.\n\n";

    // Create different types of attendance records
    DailyAttendance daily("john_doe", "2026-04-06", 8);
    MonthlyAttendanceSummary monthly("john_doe", "April 2026", 15, 2);

    // Use them polymorphically
    AttendanceRecord *records[] = {&daily, &monthly};

    for (int i = 0; i < 2; i++)
    {
        cout << records[i]->generateReport() << endl;
    }
}

void demonstrateSubscriptionPolymorphism()
{
    cout << "\n========== SUBSCRIPTION POLYMORPHISM ==========\n";
    cout << "Different subscription types override getDetails() differently.\n\n";

    BasicSubscription basic("Basic Plan", "basic123");
    PremiumSubscription premium("Premium Plan", "premium123");
    EnterpriseSubscription enterprise("Enterprise Plan", "enterprise123");

    // Store in base class array
    Subscription *subscriptions[] = {&basic, &premium, &enterprise};

    for (int i = 0; i < 3; i++)
    {
        cout << subscriptions[i]->getDetails() << endl;
    }
}

void demonstrateRequestPolymorphism()
{
    cout << "\n========== REQUEST POLYMORPHISM ==========\n";
    cout << "Different request types override getRequestDetails() differently.\n\n";

    SubscriptionRequest subReq1("john_doe", "Premium Plan", "2026-04-05");
    SubscriptionRequest subReq2("jane_smith", "Enterprise Plan", "2026-04-06");

    // Use polymorphically
    Request *requests[] = {&subReq1, &subReq2};

    for (int i = 0; i < 2; i++)
    {
        cout << requests[i]->getRequestDetails() << endl;
    }

    cout << "Approving first request...\n";
    requests[0]->approve();
    cout << "\nRejecting second request...\n";
    requests[1]->reject();
}

void demonstrateEncapsulation()
{
    cout << "\n========== ENCAPSULATION DEMONSTRATION ==========\n";
    cout << "Encapsulation hides internal details using private/protected members.\n";
    cout << "Access is provided through public methods.\n\n";

    Employee emp("john_doe", "password123", "IT", 101);

    cout << "Public method access - getEmployeeID(): " << emp.getEmployeeID() << endl;
    cout << "Public method access - getDepartment(): " << emp.getDepartment() << endl;

    cout << "\nNote: We cannot access private members like department or employeeID directly.\n";
    cout << "We must use the public getter methods provided by the class.\n";

    emp.markAttendance();
    emp.requestSubscription("Premium Plan");
}

void demonstrateVirtualDestructor()
{
    cout << "\n========== VIRTUAL DESTRUCTOR DEMONSTRATION ==========\n";
    cout << "Virtual destructors ensure proper cleanup of derived class objects.\n\n";

    // Create a derived object with base class pointer
    User *user = new Admin("dhamu", "admin123");

    cout << "Deleting Admin object through User pointer...\n";
    delete user; // Virtual destructor ensures Admin's destructor is called

    cout << "Cleanup completed properly due to virtual destructor.\n";
}

int main()
{
    cout << "\n╔══════════════════════════════════════════════════════════════╗\n";
    cout << "║     OOP CONCEPTS IN ATTENDANCE & SUBSCRIPTION SYSTEM        ║\n";
    cout << "╚══════════════════════════════════════════════════════════════╝\n";

    // Demonstrate each OOP concept
    demonstrateAbstraction();
    demonstrateInheritance();
    demonstratePolymorphism();
    demonstrateAttendancePolymorphism();
    demonstrateSubscriptionPolymorphism();
    demonstrateRequestPolymorphism();
    demonstrateEncapsulation();
    demonstrateVirtualDestructor();

    cout << "\n╔══════════════════════════════════════════════════════════════╗\n";
    cout << "║                    SUMMARY OF OOP CONCEPTS                  ║\n";
    cout << "╚══════════════════════════════════════════════════════════════╝\n\n";

    cout << "1. ABSTRACTION: Classes like User, AttendanceRecord, Subscription define\n";
    cout << "   interfaces without implementation details.\n\n";

    cout << "2. INHERITANCE: Admin and Employee inherit from User.\n";
    cout << "   DailyAttendance and MonthlyAttendanceSummary inherit from AttendanceRecord.\n";
    cout << "   BasicSubscription, PremiumSubscription inherit from Subscription.\n\n";

    cout << "3. POLYMORPHISM: Derived classes override virtual functions.\n";
    cout << "   - canMarkAttendance() behaves differently in Admin vs Employee\n";
    cout << "   - generateReport() behaves differently in daily vs monthly records\n";
    cout << "   - getDetails() behaves differently in each subscription type\n\n";

    cout << "4. ENCAPSULATION: Private members are protected from direct access.\n";
    cout << "   Public methods provide controlled access to data.\n\n";

    cout << "5. VIRTUAL DESTRUCTOR: Ensures proper cleanup of derived class objects.\n\n";

    return 0;
}
