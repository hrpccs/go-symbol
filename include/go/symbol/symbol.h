#ifndef GO_SYMBOL_SYMBOL_H
#define GO_SYMBOL_SYMBOL_H

#include <variant>
#include <elf/reader.h>

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
        using MemoryBuffer = std::variant<std::shared_ptr<elf::ISection>, std::unique_ptr<std::byte[]>, const std::byte *>;
    public:
        SymbolTable(SymbolVersion version, bool bigEndian, MemoryBuffer memoryBuffer, uint64_t base);

    public:
        SymbolIterator find(uint64_t address);
        SymbolIterator find(std::string_view name);

    public:
        SymbolEntry operator[](size_t index);

    public:
        SymbolIterator begin();
        SymbolIterator end();

    private:
        const std::byte *data();

    private:
        [[nodiscard]] uint32_t convert(uint32_t bits) const;
        [[nodiscard]] uint64_t convert(uint64_t bits) const;
        [[nodiscard]] uint64_t peek(const std::byte *ptr) const;

    private:
        bool mBigEndian;
        uint64_t mBase;
        SymbolVersion mVersion;
        MemoryBuffer mMemoryBuffer;

    private:
        uint32_t mQuantum{};
        uint32_t mPtrSize{};
        uint32_t mFuncNum{};
        uint32_t mFileNum{};

    private:
        const std::byte *mFuncNameTable{};
        const std::byte *mCuTable{};
        const std::byte *mFuncTable{};
        const std::byte *mFuncData{};
        const std::byte *mPCTable{};
        const std::byte *mFileTable{};

        friend class Symbol;
        friend class SymbolEntry;
        friend class SymbolIterator;
    };

    class Symbol {
    public:
        Symbol(const SymbolTable *table, const std::byte *buffer);

    public:
        [[nodiscard]] uint64_t entry() const;
        [[nodiscard]] const char *name() const;

    public:
        [[nodiscard]] int frameSize(uint64_t pc) const;
        [[nodiscard]] int sourceLine(uint64_t pc) const;
        [[nodiscard]] const char *sourceFile(uint64_t pc) const;

    public:
        [[nodiscard]] bool isStackTop() const;

    private:
        [[nodiscard]] uint32_t field(int n) const;

    private:
        [[nodiscard]] int value(uint32_t offset, uint64_t entry, uint64_t target) const;

    private:
        const std::byte *mBuffer;
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
        std::ptrdiff_t mSize;
        const std::byte *mBuffer;
        const SymbolTable *mTable;
    };
}

#endif //GO_SYMBOL_SYMBOL_H
