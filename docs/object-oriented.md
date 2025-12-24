# Object-Oriented Programming

BreadLang supports object-oriented programming through structs and classes.

## Structs

Structs are simple data containers with named fields. They do not support methods.

### Struct Declaration

```breadlang
struct Point {
    x: Int
    y: Int
}

struct Person {
    name: String
    age: Int
}

struct Rectangle {
    width: Double
    height: Double
    color: String
}
```

### Creating Struct Instances

```breadlang
let p: Point = Point{x: 10, y: 20}
let person: Person = Person{name: "Alice", age: 25}
let rect: Rectangle = Rectangle{width: 5.0, height: 3.0, color: "red"}
```

### Accessing Struct Fields

```breadlang
print(p.x)           // 10
print(person.name)   // "Alice"
print(rect.width)    // 5.0

// Modifying fields (if struct variable is mutable)
let origin: Point = Point{x: 0, y: 0}
origin.x = 5        // OK
origin.y = 10       // OK

// Printing structs shows all fields
print(p)  // Point { x: 10, y: 20 }
```

## Classes

Classes support inheritance, fields, and methods.

### Basic Class Declaration

```breadlang
class Animal {
    name: String
    age: Int

    // Initializer
    def init(name: String, age: Int) {
        self.name = name
        self.age = age
    }

    def speak() -> String {
        return "Some sound"
    }

    def getInfo() -> String {
        return name + " is " + str(age) + " years old"
    }
}
```

### Class Inheritance

```breadlang
class Dog extends Animal {
    breed: String

    // Initializer (must call super.init)
    def init(name: String, age: Int, breed: String) {
        super.init(name, age)
        self.breed = breed
    }

    // Override parent method
    def speak() -> String {
        return "Woof!"
    }

    // New method specific to Dog
    def wagTail() -> String {
        return name + " is wagging tail"
    }
}
```

### Creating and Using Class Instances

```breadlangs
let animal: Animal = Animal("Generic", 5)
let dog: Dog = Dog("Buddy", 3, "Golden Retriever")

// Calling methods
print(animal.speak())    // "Some sound"
print(dog.speak())       // "Woof!"
print(dog.wagTail())     // "Buddy is wagging tail"

// Accessing fields
print(dog.name)          // "Buddy"
print(dog.breed)         // "Golden Retriever"
```

## Class Features

### Method Access to Fields

Methods can access instance fields directly:

```breadlang
class Counter {
    value: Int

    def init(initial: Int) {
        self.value = initial
    }

    def increment() -> Int {
        value = value + 1  // Direct field access
        return value
    }

    def getValue() -> Int {
        return value
    }
}

let counter: Counter = Counter(0)
print(counter.increment())  // 1
print(counter.increment())  // 2
```

### Method Overriding

Child classes can override parent methods:

```breadlang
class Shape {
    def area() -> Double {
        return 0.0
    }

    def describe() -> String {
        return "A shape"
    }
}

class Circle extends Shape {
    radius: Double

    def init(radius: Double) {
        self.radius = radius
    }

    // Override parent method
    def area() -> Double {
        return 3.14159 * radius * radius
    }

    def describe() -> String {
        return "A circle with radius " + str(radius)
    }
}
```

### Single Inheritance

Classes can extend only one parent class:

```breadlang
class Vehicle {
    speed: Int

    def init(speed: Int) {
        self.speed = speed
    }
}

class Car extends Vehicle {
    doors: Int

    def init(speed: Int, doors: Int) {
        super.init(speed)
        self.doors = doors
    }
}

// Cannot extend multiple classes
// class FlyingCar extends Vehicle, Aircraft { }  // ERROR
```

## Complete Example

```breadlang
// Base class
class Employee {
    name: String
    id: Int
    salary: Double

    def init(name: String, id: Int, salary: Double) {
        self.name = name
        self.id = id
        self.salary = salary
    }

    def getInfo() -> String {
        return name + " (ID: " + str(id) + ")"
    }

    def calculatePay() -> Double {
        return salary
    }
}

// Derived class
class Manager extends Employee {
    bonus: Double

    def init(name: String, id: Int, salary: Double, bonus: Double) {
        super.init(name, id, salary)
        self.bonus = bonus
    }

    // Override parent method
    def calculatePay() -> Double {
        return salary + bonus
    }

    def manageTeam() -> String {
        return name + " is managing the team"
    }
}

// Usage
let emp: Employee = Employee("Alice", 101, 50000.0)
let mgr: Manager = Manager("Bob", 102, 70000.0, 10000.0)

print(emp.getInfo())        // "Alice (ID: 101)"
print(emp.calculatePay())   // 50000.0

print(mgr.getInfo())        // "Bob (ID: 102)"
print(mgr.calculatePay())   // 80000.0
print(mgr.manageTeam())     // "Bob is managing the team"
```

## Limitations

### No Access Modifiers

All fields and methods are public:

```breadlang
class Example {
    // No private, protected, or public keywords
    field: Int

    def init(field: Int) {
        self.field = field
    }

    def method() -> Int {
        return field
    }
}

let ex: Example = Example(42)
print(ex.field)   // Direct access to field (always allowed)
```

### No Constructor Logic

Cannot define custom constructor methods:

```breadlang
class Point {
    x: Int
    y: Int
    
    def init(x: Int, y: Int) {
        self.x = x
        self.y = y
    }
    
    // No custom constructors like __init__ or constructor()
}
```

### No Interface Support

No interface or protocol support, only single inheritance:

```breadlang
// No interface definitions
// interface Drawable { }  // Not supported

// Only single inheritance
class Shape { }
class Circle extends Shape { }  // OK
// class FlyingCircle extends Shape, Flyable { }  // ERROR
```

### Type Reporting

Type is reported as "Class" for all class instances:

```breadlang
let dog: Dog = Dog("Rex", 2, "Husky")
print(type(dog))  // "Class" (not "Dog")
```