# AddressSanitizer + UndefinedBehaviorSanitizer em todos os alvos quando RPG_SANITIZE=ON.
if(RPG_SANITIZE)
  if(MSVC)
    add_compile_options(/fsanitize=address)
  else()
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=undefined)
    add_link_options(-fsanitize=address,undefined)
  endif()
endif()
