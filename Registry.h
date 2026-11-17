
#ifndef REGISTRY_H
#define REGISTRY_H

#include <cstddef>
#include <vector>

template <typename T>
class Registry {
private:
    std::vector<T*> items_;

    // Prevent accidental shallow copies of owned pointers
    Registry(const Registry<T>& other);
    Registry<T>& operator=(const Registry<T>& other);

public:
    Registry() {}

    ~Registry() {
        this->clear();
    }

    void clear() {
        typename std::vector<T*>::iterator it;
        for (it = this->items_.begin(); it != this->items_.end(); ++it) {
            delete *it;
        }
        this->items_.clear();
    }

    // Add a dynamically allocated object pointer (takes ownership)
    T& add(T* p) {
        this->items_.push_back(p);
        return *(this->items_.back());
    }

    // Overload for derived class raw pointers (e.g., StudySeat* -> Seat*)
    template <class U>
    T& add(U* p) {
        T* basePtr = p;
        this->items_.push_back(basePtr);
        return *(this->items_.back());
    }

    // Template overload so callers passing smart-pointer wrappers (e.g. in tests.cpp) also work
    template <class Ptr>
    T& add(Ptr p) {
        T* rawPtr = p.release();
        return this->add(rawPtr);
    }

    // Find item by integer ID (overloaded find)
    T* find(int id) const {
        typename std::vector<T*>::const_iterator it;
        for (it = this->items_.begin(); it != this->items_.end(); ++it) {
            if ((*it)->id() == id) {
                return *it;
            }
        }
        return 0;
    }

    // Find the first item matching a predicate (member function template)
    template <typename Pred>
    T* find(Pred pred) const {
        typename std::vector<T*>::const_iterator it;
        for (it = this->items_.begin(); it != this->items_.end(); ++it) {
            if (pred(**it)) {
                return *it;
            }
        }
        return 0;
    }

    template <typename Fn>
    void forEach(Fn fn) const {
        typename std::vector<T*>::const_iterator it;
        for (it = this->items_.begin(); it != this->items_.end(); ++it) {
            fn(**it);
        }
    }

    std::size_t size() const {
        return this->items_.size();
    }

    T* at(std::size_t i) const {
        return i < this->items_.size() ? this->items_[i] : 0;
    }

    T* operator[](std::size_t i) const {
        return this->at(i);
    }

    typename std::vector<T*>::iterator begin() {
        return this->items_.begin();
    }

    typename std::vector<T*>::iterator end() {
        return this->items_.end();
    }

    typename std::vector<T*>::const_iterator begin() const {
        return this->items_.begin();
    }

    typename std::vector<T*>::const_iterator end() const {
        return this->items_.end();
    }
};

#endif
