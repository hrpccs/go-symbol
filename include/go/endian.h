#ifndef GO_SYMBOL_ENDIAN_H
#define GO_SYMBOL_ENDIAN_H

#include <elf/endian.h>
#include <stdexcept>

namespace go::endian {
    class Converter {
    public:
        explicit Converter(elf::endian::Type endian) : mEndian(endian) {

        }

    public:
        template<typename T>
        T operator()(T bits) const {
            if (mEndian == elf::endian::Little)
                return elf::endian::little(bits);

            return elf::endian::big(bits);
        }

        uint64_t operator()(const void *ptr, size_t size) const {
            switch (size) {
                case 2:
                    return operator()(*(uint16_t *) ptr);

                case 4:
                    return operator()(*(uint32_t *) ptr);

                case 8:
                    return operator()(*(uint64_t *) ptr);

                default:
                    throw std::invalid_argument("invalid size");
            }
        }

    private:
        elf::endian::Type mEndian;
    };
}

#endif //GO_SYMBOL_ENDIAN_H
