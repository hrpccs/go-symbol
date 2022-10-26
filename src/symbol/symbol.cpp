#include <go/symbol/symbol.h>
#include <go/binary/binary.h>
#include <algorithm>
#include <cstring>

constexpr auto STACK_TOP_FUNCTION = {
        "runtime.mstart",
        "runtime.rt0_go",
        "runtime.mcall",
        "runtime.morestack",
        "runtime.lessstack",
        "runtime.asmcgocall",
        "runtime.externalthreadhandler",
        "runtime.goexit"
};

go::symbol::SymbolTable::SymbolTable(SymbolVersion version, bool bigEndian, MemoryBuffer memoryBuffer, uint64_t base)
        : mVersion(version), mBigEndian(bigEndian), mMemoryBuffer(std::move(memoryBuffer)), mBase(base) {
    const std::byte *buffer = data();

    mQuantum = std::to_integer<uint32_t>(buffer[6]);
    mPtrSize = std::to_integer<uint32_t>(buffer[7]);

    switch (mVersion) {
        case VERSION12: {
            mFuncNum = peek(buffer + 8);
            mFuncData = buffer;
            mFuncNameTable = buffer;
            mFuncTable = buffer + 8 + mPtrSize;
            mPCTable = buffer;

            uint32_t funcTableSize = mFuncNum * 2 * mPtrSize + mPtrSize;
            uint32_t fileOffset = convert(*(uint32_t *) (mFuncTable + funcTableSize));

            mFileTable = buffer + fileOffset;
            mFileNum = convert(*(uint32_t *) mFileTable);

            break;
        }

        case VERSION116: {
            mFuncNum = peek(buffer + 8);
            mFileNum = peek(buffer + 8 + mPtrSize);

            mFuncNameTable = buffer + peek(buffer + 8 + 2 * mPtrSize);
            mCuTable = buffer + peek(buffer + 8 + 3 * mPtrSize);
            mFileTable = buffer + peek(buffer + 8 + 4 * mPtrSize);
            mPCTable = buffer + peek(buffer + 8 + 5 * mPtrSize);
            mFuncData = buffer + peek(buffer + 8 + 6 * mPtrSize);
            mFuncTable = buffer + peek(buffer + 8 + 6 * mPtrSize);

            break;
        }

        case VERSION118:
        case VERSION120: {
            mFuncNum = peek(buffer + 8);
            mFileNum = peek(buffer + 8 + mPtrSize);

            mBase += peek(buffer + 8 + 2 * mPtrSize);

            mFuncNameTable = buffer + peek(buffer + 8 + 3 * mPtrSize);
            mCuTable = buffer + peek(buffer + 8 + 4 * mPtrSize);
            mFileTable = buffer + peek(buffer + 8 + 5 * mPtrSize);
            mPCTable = buffer + peek(buffer + 8 + 6 * mPtrSize);
            mFuncData = buffer + peek(buffer + 8 + 7 * mPtrSize);
            mFuncTable = buffer + peek(buffer + 8 + 7 * mPtrSize);

            break;
        }
    }
}

go::symbol::SymbolIterator go::symbol::SymbolTable::find(uint64_t address) {
    return std::upper_bound(begin(), end(), address, [&](uint64_t value, const auto &entry) {
        return value < entry.symbol().entry();
    });
}

go::symbol::SymbolIterator go::symbol::SymbolTable::find(std::string_view name) {
    return std::find_if(begin(), end(), [=](const auto &entry) {
        return name == entry.symbol().name();
    });
}

go::symbol::SymbolIterator go::symbol::SymbolTable::begin() {
    return {this, mFuncTable};
}

go::symbol::SymbolIterator go::symbol::SymbolTable::end() {
    return begin() + mFuncNum;
}

const std::byte *go::symbol::SymbolTable::data() {
    size_t index = mMemoryBuffer.index();

    if (index == 0) {
        return std::get<std::shared_ptr<elf::ISection>>(mMemoryBuffer)->data();
    } else if (index == 1) {
        return std::get<std::unique_ptr<std::byte[]>>(mMemoryBuffer).get();
    }

    return std::get<const std::byte *>(mMemoryBuffer);
}

uint32_t go::symbol::SymbolTable::convert(uint32_t bits) const {
    return mBigEndian ? be32toh(bits) : le32toh(bits);
}

uint64_t go::symbol::SymbolTable::convert(uint64_t bits) const {
    return mBigEndian ? be64toh(bits) : le64toh(bits);
}

uint64_t go::symbol::SymbolTable::peek(const std::byte *ptr) const {
    if (mPtrSize == 4)
        return convert(*(uint32_t *) ptr);

    return convert(*(uint64_t *) ptr);
}

go::symbol::Symbol::Symbol(const go::symbol::SymbolTable *table, const std::byte *buffer) : mTable(table), mBuffer(buffer) {

}

uint64_t go::symbol::Symbol::entry() const {
    if (mTable->mVersion < VERSION118)
        return mTable->mBase + mTable->peek(mBuffer);

    return mTable->mBase + mTable->convert(*(uint32_t *) mBuffer);
}

const char *go::symbol::Symbol::name() const {
    return (const char *) mTable->mFuncNameTable + field(1);
}

int go::symbol::Symbol::frameSize(uint64_t pc) const {
    uint32_t sp = field(4);

    if (sp == 0)
        return 0;

    int x = value(sp, entry(), pc);

    if (x == -1)
        return 0;

    if (x & (mTable->mPtrSize - 1))
        return 0;

    return x;
}

int go::symbol::Symbol::sourceLine(uint64_t pc) const {
    return value(field(6), entry(), pc);
}

const char *go::symbol::Symbol::sourceFile(uint64_t pc) const {
    int n = value(field(5), entry(), pc);

    if (n < 0 || n > mTable->mFileNum)
        return "";

    if (mTable->mVersion == VERSION12) {
        if (n == 0)
            return "";

        return (const char *) mTable->mFuncData + *(int *) (mTable->mFileTable + n * 4);
    }

    uint32_t offset = *(uint32_t *) (mTable->mCuTable + (field(8) + n) * 4);

    if (!offset)
        return "";

    return (const char *) mTable->mFileTable + offset;
}

bool go::symbol::Symbol::isStackTop() const {
    return std::any_of(STACK_TOP_FUNCTION.begin(), STACK_TOP_FUNCTION.end(), [name = name()](const auto &func) {
        return strcmp(func, name) == 0;
    });
}

uint32_t go::symbol::Symbol::field(int n) const {
    return mTable->convert(*(uint32_t *) (mBuffer + (mTable->mVersion >= VERSION118 ? 4 : mTable->mPtrSize) + (n - 1) * 4));
}

int go::symbol::Symbol::value(uint32_t offset, uint64_t entry, uint64_t target) const {
    const std::byte *buffer = mTable->mPCTable + offset;

    int value = -1;
    uint64_t pc = entry;

    while (true) {
        std::optional<std::pair<int64_t, int>> result = binary::varInt(buffer);

        if (!result)
            return -1;

        if (result->first == 0 && pc != entry)
            return -1;

        value += int(result->first);
        buffer += result->second;

        result = binary::uVarInt(buffer);

        if (!result)
            return -1;

        pc += result->first * mTable->mQuantum;
        buffer += result->second;

        if (target < pc)
            break;
    }

    return value;
}

go::symbol::SymbolEntry::SymbolEntry(const go::symbol::SymbolTable *table, uint64_t entry, uint64_t offset) : mTable(table), mEntry(entry), mOffset(offset) {

}

uint64_t go::symbol::SymbolEntry::entry() const {
    return mEntry;
}

go::symbol::Symbol go::symbol::SymbolEntry::symbol() const {
    return {mTable, mTable->mFuncData + mOffset};
}

go::symbol::SymbolIterator::SymbolIterator(const go::symbol::SymbolTable *table, const std::byte *buffer) : mTable(table), mBuffer(buffer), mSize(table->mVersion >= VERSION118 ? 4 : table->mPtrSize) {

}

go::symbol::SymbolEntry go::symbol::SymbolIterator::operator*() {
    auto peek = [=](const std::byte *address) -> uint64_t {
        if (mSize == 4)
            return mTable->convert(*(uint32_t *) address);

        return mTable->convert(*(uint64_t *) address);
    };

    return {mTable, mTable->mBase + peek(mBuffer), peek(mBuffer + mSize)};
}

go::symbol::SymbolIterator &go::symbol::SymbolIterator::operator--() {
    mBuffer -= 2 * mSize;
    return *this;
}

go::symbol::SymbolIterator &go::symbol::SymbolIterator::operator++() {
    mBuffer += 2 * mSize;
    return *this;
}

go::symbol::SymbolIterator &go::symbol::SymbolIterator::operator+=(std::ptrdiff_t offset) {
    mBuffer += offset * 2 * mSize;
    return *this;
}

go::symbol::SymbolIterator go::symbol::SymbolIterator::operator+(std::ptrdiff_t offset) {
    return {mTable, mBuffer + offset * 2 * mSize};
}

bool go::symbol::SymbolIterator::operator==(const go::symbol::SymbolIterator &rhs) {
    return mBuffer == rhs.mBuffer;
}

bool go::symbol::SymbolIterator::operator!=(const go::symbol::SymbolIterator &rhs) {
    return !operator==(rhs);
}

std::ptrdiff_t go::symbol::SymbolIterator::operator-(const go::symbol::SymbolIterator &rhs) {
    return (mBuffer - rhs.mBuffer) / 2 * mSize;
}