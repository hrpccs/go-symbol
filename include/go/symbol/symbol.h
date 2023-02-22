#ifndef GO_SYMBOL_SYMBOL_H
#define GO_SYMBOL_SYMBOL_H

#include <variant>
#include <elf/reader.h>
#include <go/endian.h>

namespace go::symbol {
    enum SymbolVersion {
        VERSION12,
        VERSION116,
        VERSION118,
        VERSION120
    };

    class SymbolEntry;
    class SymbolIterator;

    class SymbolTable {
    public:
        SymbolTable(
                SymbolVersion
                version,
                endian::Converter converter,
                int fd,
                off64_t offset,
                uint64_t address,
                uint64_t base
        );

        SymbolTable(SymbolTable &&) = default;
        ~SymbolTable();

    public:
        [[nodiscard]] SymbolIterator find(uint64_t address) const;
        [[nodiscard]] SymbolIterator find(std::string_view name) const;

    public:
        [[nodiscard]] size_t size() const;

    public:
        [[nodiscard]] SymbolEntry operator[](size_t index) const;

    public:
        [[nodiscard]] SymbolIterator begin() const;
        [[nodiscard]] SymbolIterator end() const;

    private:
        int mFD;
        uint64_t mBase;
        off64_t mOffset;
        uint64_t mAddress;
        SymbolVersion mVersion;
        endian::Converter mConverter;
        std::unique_ptr<std::byte[]> mFuncTableBuffer;

    private:
        uint32_t mQuantum{};
        uint32_t mPtrSize{};
        uint32_t mFuncNum{};
        uint32_t mFileNum{};

    private:
        uint64_t mFuncNameTable{};
        uint64_t mCuTable{};
        uint64_t mFuncTable{};
        uint64_t mFuncData{};
        uint64_t mPCTable{};
        uint64_t mFileTable{};

        friend class Symbol;
        friend class SymbolEntry;
        friend class SymbolIterator;
    };

    class Symbol {
    public:
        Symbol(const SymbolTable *table, uint64_t address);

    public:
        [[nodiscard]] uint64_t entry() const;
        [[nodiscard]] std::string name() const;

    public:
        [[nodiscard]] int frameSize(uint64_t pc) const;
        [[nodiscard]] int sourceLine(uint64_t pc) const;
        [[nodiscard]] std::string sourceFile(uint64_t pc) const;

    public:
        [[nodiscard]] bool isStackTop() const;

    private:
        [[nodiscard]] uint32_t field(int n) const;

    private:
        [[nodiscard]] int value(uint32_t offset, uint64_t entry, uint64_t target) const;

    private:
        uint64_t mAddress;
        const SymbolTable *mTable;
    };

    class SymbolEntry {
    public:
        SymbolEntry(const SymbolTable *table, uint64_t entry, uint64_t offset);

    public:
        [[nodiscard]] uint64_t entry() const;
        [[nodiscard]] Symbol symbol() const;

    private:
        uint64_t mEntry;
        uint64_t mOffset;
        const SymbolTable *mTable;
    };

    class SymbolIterator {
    public:
        using difference_type = std::ptrdiff_t;
        using value_type = SymbolEntry;
        using pointer = value_type *;
        using reference = value_type &;
        using iterator_category = std::random_access_iterator_tag;

    public:
        SymbolIterator(const SymbolTable *table, const std::byte *buffer);

    public:
        SymbolEntry operator*();
        SymbolIterator &operator--();
        SymbolIterator &operator++();
        SymbolIterator &operator+=(std::ptrdiff_t offset);
        SymbolIterator operator-(std::ptrdiff_t offset);
        SymbolIterator operator+(std::ptrdiff_t offset);

    public:
        bool operator==(const SymbolIterator &rhs);
        bool operator!=(const SymbolIterator &rhs);

    public:
        std::ptrdiff_t operator-(const SymbolIterator &rhs);

    private:
        size_t mSize;
        const std::byte *mBuffer;
        const SymbolTable *mTable;
    };
}

#endif //GO_SYMBOL_SYMBOL_H
