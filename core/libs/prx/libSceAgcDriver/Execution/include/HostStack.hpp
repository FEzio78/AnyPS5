#ifndef CORE_LIBS_PRX_LIBSCEAGCDRIVER_EXECUTION_INCLUDE_HOSTSTACK_HPP
#define CORE_LIBS_PRX_LIBSCEAGCDRIVER_EXECUTION_INCLUDE_HOSTSTACK_HPP

#include <exception>
#include <optional>
#include <type_traits>
#include <utility>

namespace AgcDriver {

bool OnHostStack();
void RunOnHostStack(void (*function)(void*), void* argument);

template<typename TFunction>
decltype(auto) WithHostStack(TFunction&& function) {
    using Result = std::invoke_result_t<TFunction&>;
    if (OnHostStack()) return std::forward<TFunction>(function)();
    std::exception_ptr error;
    if constexpr (std::is_void_v<Result>) {
        struct Call {
            TFunction* function;
            std::exception_ptr* error;
        } call{&function, &error};
        RunOnHostStack([](void* raw) {
            auto* call = static_cast<Call*>(raw);
            try {
                (*call->function)();
            } catch (...) {
                *call->error = std::current_exception();
            }
        }, &call);
        if (error) std::rethrow_exception(error);
    } else {
        std::optional<Result> result;
        struct Call {
            TFunction* function;
            std::optional<Result>* result;
            std::exception_ptr* error;
        } call{&function, &result, &error};
        RunOnHostStack([](void* raw) {
            auto* call = static_cast<Call*>(raw);
            try {
                call->result->emplace((*call->function)());
            } catch (...) {
                *call->error = std::current_exception();
            }
        }, &call);
        if (error) std::rethrow_exception(error);
        return std::move(*result);
    }
}

}

#endif
