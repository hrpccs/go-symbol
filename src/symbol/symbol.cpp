#include <go/symbol/symbol.h>
#include <go/binary.h>
#include <algorithm>
#include <cstring>
#include <unistd.h>

constexpr auto MAX_VAR_INT_LENGTH = 10;

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

go::symbol::SymbolTable::SymbolTable(
        SymbolVersion version,
        endian::Converter converter,
        int fd,
        off64_t offset,
        uint64_t address,
        uint64_t base
) : mVersion(version), mConverter(converter), mFD(fd), mOffset(offset), mAddress(address), mBase(base) {
    std::byte buffer[128];

    lseek64(mFD, offset, SEEK_SET);
    read(mFD, buffer, sizeof(buffer));

    mQuantum = std::to_integer<uint32_t>(buffer[6]);
    mPtrSize = std::to_integer<uint32_t>(buffer[7]);

    switch (mVersion) {
        case VERSION12: {
            mFuncNum = mConverter(buffer + 8, mPtrSize);
            mFuncData = mAddress;
            mFuncNameTable = mAddress;
            mFuncTable = mAddress + 8 + mPtrSize;
            mPCTable = mAddress;

            uint32_t funcTableSize = mFuncNum * 2 * mPtrSize + mPtrSize;
            uint32_t fileOffset = 0;

            lseek64(mFD, mOffset + (off64_t) (mFuncTable + funcTableSize - mAddress), SEEK_SET);
            read(mFD, &fileOffset, sizeof(uint32_t));
            fileOffset = mConverter(fileOffset);

            mFileTable = mAddress + fileOffset;

            lseek64(mFD, mOffset + (off64_t) (mFileTable - mAddress), SEEK_SET);
            read(mFD, &mFileNum, sizeof(uint32_t));
            mFileNum = mConverter(mFileNum);

            break;
        }

        case VERSION116: {
            mFuncNum = mConverter(buffer + 8, mPtrSize);
            mFileNum = mConverter(buffer + 8 + mPtrSize, mPtrSize);

            mFuncNameTable = mAddress + mConverter(buffer + 8 + 2 * mPtrSize, mPtrSize);
            mCuTable = mAddress + mConverter(buffer + 8 + 3 * mPtrSize, mPtrSize);
            mFileTable = mAddress + mConverter(buffer + 8 + 4 * mPtrSize, mPtrSize);
            mPCTable = mAddress + mConverter(buffer + 8 + 5 * mPtrSize, mPtrSize);
            mFuncData = mAddress + mConverter(buffer + 8 + 6 * mPtrSize, mPtrSize);
            mFuncTable = mAddress + mConverter(buffer + 8 + 6 * mPtrSize, mPtrSize);

            break;
        }

        case VERSION118:
        case VERSION120: {
            mFuncNum = mConverter(buffer + 8, mPtrSize);
            mFileNum = mConverter(buffer + 8 + mPtrSize, mPtrSize);

            mBase += mConverter(buffer + 8 + 2 * mPtrSize, mPtrSize);

            mFuncNameTable = mAddress + mConverter(buffer + 8 + 3 * mPtrSize, mPtrSize);
            mCuTable = mAddress + mConverter(buffer + 8 + 4 * mPtrSize, mPtrSize);
            mFileTable = mAddress + mConverter(buffer + 8 + 5 * mPtrSize, mPtrSize);
            mPCTable = mAddress + mConverter(buffer + 8 + 6 * mPtrSize, mPtrSize);
            mFuncData = mAddress + mConverter(buffer + 8 + 7 * mPtrSize, mPtrSize);
            mFuncTable = mAddress + mConverter(buffer + 8 + 7 * mPtrSize, mPtrSize);

            break;
        }
    }

    uint64_t size = (mFuncNum + 1) * 2 * (mVersion >= VERSION118 ? 4 : mPtrSize);
    mFuncTableBuffer = std::make_unique<std::byte[]>(size);

    lseek64(mFD, mOffset + (off64_t) (mFuncTable - mAddress), SEEK_SET);
    read(mFD, mFuncTableBuffer.get(), size);
}

go::symbol::SymbolTable::~SymbolTable() {
    close(mFD);
}

go::symbol::SymbolIterator go::symbol::SymbolTable::find(uint64_t address) const {
    if (address < operator[](0).entry() || address >= operator[](mFuncNum).entry())
        return end();

    return std::upper_bound(begin(), end() + 1, address, [](uint64_t value, const auto &entry) {
        return value < entry.entry();
    }) - 1;
}

go::symbol::SymbolIterator go::symbol::SymbolTable::find(std::string_view name) const {
    return std::find_if(begin(), end(), [=](const auto &entry) {
        return name == entry.symbol().name();
    });
}

size_t go::symbol::SymbolTable::size() const {
    return mFuncNum;
}

go::symbol::SymbolEntry go::symbol::SymbolTable::operator[](size_t index) const {
    return *(begin() + std::ptrdiff_t(index));
}

go::symbol::SymbolIterator go::symbol::SymbolTable::begin() const {
    return {this, mFuncTableBuffer.get()};
}

go::symbol::SymbolIterator go::symbol::SymbolTable::end() const {
    return begin() + mFuncNum;
}

go::symbol::Symbol::Symbol(const go::symbol::SymbolTable *table, uint64_t address) : mTable(table), mAddress(address) {

}

uint64_t go::symbol::Symbol::entry() const {
    if (mTable->mVersion < VERSION118) {
        std::byte buffer[8] = {};

        lseek64(mTable->mFD, mTable->mOffset + (off64_t) (mAddress - mTable->mAddress), SEEK_SET);
        read(mTable->mFD, &buffer, mTable->mPtrSize);

        if (mTable->mPtrSize == 4)
            return mTable->mBase + mTable->mConverter(*(uint32_t *) buffer);

        return mTable->mBase + mTable->mConverter(*(uint64_t *) buffer);
    }

    uint32_t entry;

    lseek64(mTable->mFD, mTable->mOffset + (off64_t) (mAddress - mTable->mAddress), SEEK_SET);
    read(mTable->mFD, &entry, sizeof(uint32_t));

    return mTable->mBase + mTable->mConverter(entry);
}

std::string go::symbol::Symbol::name() const {
    lseek64(mTable->mFD, mTable->mOffset + (off64_t) (mTable->mFuncNameTable + field(1) - mTable->mAddress), SEEK_SET);

    std::string name;

    while (true) {
        char c;

        if (read(mTable->mFD, &c, sizeof(char)) != 1 || !c)
            break;

        name.push_back(c);
    }

    return name;
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

std::string go::symbol::Symbol::sourceFile(uint64_t pc) const {
    int n = value(field(5), entry(), pc);

    if (n < 0 || n > mTable->mFileNum)
        return "";

    if (mTable->mVersion == VERSION12) {
        if (n == 0)
            return "";

        int offset;

        lseek64(mTable->mFD, mTable->mOffset + (off64_t) (mTable->mFileTable + n * 4 - mTable->mAddress), SEEK_SET);
        read(mTable->mFD, &offset, sizeof(int));

        offset = mTable->mConverter(offset);
        lseek64(mTable->mFD, mTable->mOffset + (off64_t) (mTable->mFuncData + offset - mTable->mAddress), SEEK_SET);

        std::string name;

        while (true) {
            char c;

            if (read(mTable->mFD, &c, sizeof(char)) != 1 || !c)
                break;

            name.push_back(c);
        }

        return name;
    }

    uint32_t offset;

    lseek64(
            mTable->mFD,
            mTable->mOffset + (off64_t) (mTable->mCuTable + (field(8) + n) * 4 - mTable->mAddress),
            SEEK_SET
    );

    read(mTable->mFD, &offset, sizeof(uint32_t));
    offset = mTable->mConverter(offset);

    if (!offset)
        return "";

    lseek64(mTable->mFD, mTable->mOffset + (off64_t) (mTable->mFileTable + offset - mTable->mAddress), SEEK_SET);

    std::string name;

    while (true) {
        char c;

        if (read(mTable->mFD, &c, sizeof(char)) != 1 || !c)
            break;

        name.push_back(c);
    }

    return name;
}

bool go::symbol::Symbol::isStackTop() const {
    return std::any_of(STACK_TOP_FUNCTION.begin(), STACK_TOP_FUNCTION.end(), [name = name()](const auto &func) {
        return name == func;
    });
}

uint32_t go::symbol::Symbol::field(int n) const {
    lseek64(
            mTable->mFD,
            mTable->mOffset +
            (off64_t) (mAddress + (mTable->mVersion >= VERSION118 ? 4 : mTable->mPtrSize) + (n - 1) * 4 -
                       mTable->mAddress),
            SEEK_SET
    );

    uint32_t value;
    read(mTable->mFD, &value, sizeof(uint32_t));

    return mTable->mConverter(value);
}

int go::symbol::Symbol::value(uint32_t offset, uint64_t entry, uint64_t target) const {
    lseek64(
            mTable->mFD,
            mTable->mOffset + (off64_t) (mTable->mPCTable + offset - mTable->mAddress),
            SEEK_SET
    );

    int length = 0;
    std::byte buffer[1024];

    read(mTable->mFD, buffer, sizeof(buffer));

    int value = -1;
    uint64_t pc = entry;

    while (true) {
        std::optional<std::pair<int64_t, int>> result = binary::varInt(buffer + length);

        if (!result)
            return -1;

        if (result->first == 0 && pc != entry)
            return -1;

        value += int(result->first);
        length += result->second;

        result = binary::uVarInt(buffer + length);

        if (!result)
            return -1;

        pc += result->first * mTable->mQuantum;
        length += result->second;

        if (target < pc)
            break;

        if (sizeof(buffer) - length >= 2 * MAX_VAR_INT_LENGTH)
            continue;

        memcpy(buffer, buffer + length, sizeof(buffer) - length);
        read(mTable->mFD, buffer + sizeof(buffer) - length, length);

        length = 0;
    }

    return value;
}

go::symbol::SymbolEntry::SymbolEntry(const go::symbol::SymbolTable *table, uint64_t entry, uint64_t offset)
        : mTable(table), mEntry(entry), mOffset(offset) {

}

uint64_t go::symbol::SymbolEntry::entry() const {
    return mEntry;
}

go::symbol::Symbol go::symbol::SymbolEntry::symbol() const {
    return {mTable, mTable->mFuncData + mOffset};
}

go::symbol::SymbolIterator::SymbolIterator(const go::symbol::SymbolTable *table, const std::byte *buffer)
        : mTable(table), mBuffer(buffer), mSize(table->mVersion >= VERSION118 ? 4 : table->mPtrSize) {

}

go::symbol::SymbolEntry go::symbol::SymbolIterator::operator*() {
    return {
            mTable,
            mTable->mBase + mTable->mConverter(mBuffer, mSize),
            mTable->mConverter(mBuffer + mSize, mSize)
    };
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

go::symbol::SymbolIterator go::symbol::SymbolIterator::operator-(std::ptrdiff_t offset) {
    return {mTable, mBuffer - offset * 2 * mSize};
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
    return (mBuffer - rhs.mBuffer) / std::ptrdiff_t(2 * mSize);
}