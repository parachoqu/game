# Avisos do projeto, aplicados a cada alvo nosso (dependências externas entram como SYSTEM).
function(rpg_set_warnings target)
  if(MSVC)
    set(flags /W4 /permissive- /w14242 /w14254 /w14263 /w14265 /w14287 /w14296 /w14311 /w14545
              /w14546 /w14547 /w14549 /w14555 /w14619 /w14640 /w14826 /w14905 /w14906 /w14928)
    if(RPG_WARNINGS_AS_ERRORS)
      list(APPEND flags /WX)
    endif()
  else()
    set(flags -Wall -Wextra -Wpedantic -Wshadow -Wnon-virtual-dtor -Wold-style-cast -Wcast-align
              -Wunused -Woverloaded-virtual -Wdouble-promotion -Wformat=2
              -Wimplicit-fallthrough -Wmisleading-indentation)
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
      list(APPEND flags -Wduplicated-cond -Wduplicated-branches -Wlogical-op -Wuseless-cast)
    else()
      # No GCC com -O2 este aviso acusa falsos positivos dentro da libstdc++ (std::string/optional).
      list(APPEND flags -Wnull-dereference)
    endif()
    if(RPG_WARNINGS_AS_ERRORS)
      list(APPEND flags -Werror)
    endif()
  endif()
  target_compile_options(${target} PRIVATE ${flags})
endfunction()
