#pragma once
#include <variant>
#include <utility>

template<typename T, typename E>
class [[nodiscard]] Result {
    std::variant<T,E> v_;
    explicit Result(std::variant<T,E>&& v) : v_(std::move(v)) {}
public:
    static Result ok (T val) {return Result{ std::variant<T,E>{std::in_place_index<0>, std::move(val)} };}
    static Result err(E e) {return Result{std::variant<T,E>{std::in_place_index<1>, std::move(e)}}; }

    bool has_value() const noexcept {return v_.index() == 0;}
    explicit operator bool() const noexcept {return has_value();}

    T& value() noexcept {return *std::get_if<0>(&v_);}
    const T& value() const noexcept {return *std::get_if<0>(&v_);}

    E& error() noexcept { return *std::get_if<1>(&v_);}
    const E& error() const noexcept {return *std::get_if<1>(&v_);}
};
