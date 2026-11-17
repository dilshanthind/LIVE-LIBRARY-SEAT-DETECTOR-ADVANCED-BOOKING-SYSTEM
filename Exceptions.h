
#ifndef EXCEPTIONS_H
#define EXCEPTIONS_H

#include <exception>
#include <string>

class LibraryError : public std::exception {
protected:
    std::string message_;

public:
    LibraryError() : message_("Library error") {}
    LibraryError(const std::string& msg) : message_(msg) {}
    LibraryError(const LibraryError& other) : std::exception(other), message_(other.message_) {}
    virtual ~LibraryError() throw() {}

    LibraryError& operator=(const LibraryError& other) {
        if (this != &other) {
            this->message_ = other.message_;
        }
        return *this;
    }

    virtual const char* what() const throw() {
        return this->message_.c_str();
    }

    virtual const char* code() const throw() {
        return "LIBRARY_ERROR";
    }

    virtual int httpStatus() const throw() {
        return 500;
    }
};

class ValidationError : public LibraryError {
public:
    ValidationError() : LibraryError("Validation error") {}
    ValidationError(const std::string& msg) : LibraryError(msg) {}
    ValidationError(const ValidationError& other) : LibraryError(other) {}
    virtual ~ValidationError() throw() {}

    virtual const char* code() const throw() {
        return "VALIDATION_ERROR";
    }

    virtual int httpStatus() const throw() {
        return 400;
    }
};

class NotFoundError : public LibraryError {
public:
    NotFoundError() : LibraryError("Not found") {}
    NotFoundError(const std::string& msg) : LibraryError(msg) {}
    NotFoundError(const NotFoundError& other) : LibraryError(other) {}
    virtual ~NotFoundError() throw() {}

    virtual const char* code() const throw() {
        return "NOT_FOUND";
    }

    virtual int httpStatus() const throw() {
        return 404;
    }
};

class SlotConflictError : public LibraryError {
public:
    SlotConflictError() : LibraryError("Slot conflict") {}
    SlotConflictError(const std::string& msg) : LibraryError(msg) {}
    SlotConflictError(const SlotConflictError& other) : LibraryError(other) {}
    virtual ~SlotConflictError() throw() {}

    virtual const char* code() const throw() {
        return "SLOT_CONFLICT";
    }

    virtual int httpStatus() const throw() {
        return 409;
    }
};

class CameraError : public LibraryError {
public:
    CameraError() : LibraryError("Camera error") {}
    CameraError(const std::string& msg) : LibraryError(msg) {}
    CameraError(const CameraError& other) : LibraryError(other) {}
    virtual ~CameraError() throw() {}

    virtual const char* code() const throw() {
        return "CAMERA_ERROR";
    }

    virtual int httpStatus() const throw() {
        return 503;
    }
};

#endif
