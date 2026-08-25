# Санитайзеры глобальны намеренно: смешение инструментированного и чистого
# кода даёт ложные срабатывания и пропуски, поэтому add_compile_options,
# а не INTERFACE-таргет.

if(L2_SANITIZE AND L2_TSAN)
    message(FATAL_ERROR "L2_SANITIZE and L2_TSAN are mutually exclusive: "
                        "ASan and TSan cannot instrument the same binary")
endif()

if(L2_SANITIZE)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -g)
    add_link_options(-fsanitize=address,undefined)
endif()

if(L2_TSAN)
    add_compile_options(-fsanitize=thread -fno-omit-frame-pointer -g)
    add_link_options(-fsanitize=thread)
endif()
