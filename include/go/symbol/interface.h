#ifndef GO_SYMBOL_INTERFACE_H
#define GO_SYMBOL_INTERFACE_H

#include <go/endian.h>
#include <go/version.h>
#include <elf/reader.h>

namespace go::symbol {
    class Interface;
    class InterfaceIterator;

    class InterfaceTable {
    public:
        InterfaceTable(
                elf::Reader reader,
                std::shared_ptr<elf::ISection> section,
                Version version,
                uint64_t types,
                uint64_t base,
                size_t ptrSize,
                endian::Converter converter
        );

    public:
        [[nodiscard]] size_t size() const;

    public:
        [[nodiscard]] Interface operator[](size_t index) const;

    public:
        [[nodiscard]] InterfaceIterator begin() const;
        [[nodiscard]] InterfaceIterator end() const;

    private:
        uint64_t mBase;
        uint64_t mTypes;
        size_t mPtrSize;
        Version mVersion;
        endian::Converter mConverter;
        elf::Reader mReader;
        std::shared_ptr<elf::ISection> mSection;

        friend class Interface;
        friend class InterfaceIterator;
    };

    class Interface {
    public:
        Interface(const InterfaceTable *table, uint64_t address);

    public:
        [[nodiscard]] uint64_t address() const;
        [[nodiscard]] uint64_t methodCount() const;
        [[nodiscard]] uint64_t method(uint64_t index) const;

    public:
        [[nodiscard]] std::optional<std::string> name() const;
        [[nodiscard]] std::optional<std::string> interfaceName() const;

    private:
        [[nodiscard]] std::optional<std::string> typeName(const std::byte *buffer) const;

    private:
        uint64_t mAddress;
        const go::symbol::InterfaceTable *mTable;
    };

    class InterfaceIterator {
    public:
        using difference_type = std::ptrdiff_t;
        using value_type = Interface;
        using pointer = value_type *;
        using reference = value_type &;
        using iterator_category = std::random_access_iterator_tag;

    public:
        InterfaceIterator(const InterfaceTable *table, const std::byte *buffer);

    public:
        Interface operator*();
        InterfaceIterator &operator--();
        InterfaceIterator &operator++();
        InterfaceIterator &operator+=(std::ptrdiff_t offset);
        InterfaceIterator operator-(std::ptrdiff_t offset);
        InterfaceIterator operator+(std::ptrdiff_t offset);

    public:
        bool operator==(const InterfaceIterator &rhs);
        bool operator!=(const InterfaceIterator &rhs);

    public:
        std::ptrdiff_t operator-(const InterfaceIterator &rhs);

    private:
        const std::byte *mBuffer;
        const InterfaceTable *mTable;
    };
}

#endif //GO_SYMBOL_INTERFACE_H
