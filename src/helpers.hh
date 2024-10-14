#ifndef _HELPERS_HH
#define _HELPERS_HH

template<class... Ts> struct cases : Ts... {
    using Ts::operator()...;
};

template<class... Ts> cases(Ts...) -> cases<Ts...>;

#endif