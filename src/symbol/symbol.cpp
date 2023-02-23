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
        MemoryBuffer memoryBuffer,
        uint64_t base
) : mVersion(version), mConverter(converter), mMemoryBuffer(std::move(memoryBuffer)), mBase(base) {
    const std::byte *buffer = data();

    mQuantum = std::to_integer<uint32_t>(buffer[6]);
    mPtrSize = std::to_integer<uint32_t>(buffer[7]);

    switch (mVersion) {
        case VERSION12: {
            mFuncNum = mConverter(buffer + 8, mPtrSize);
            mFuncData = buffer;
            mFuncNameTable = buffer;
            mFuncTable = buffer + 8 + mPtrSize;
            mPCTable = buffer;

            uint32_t funcTableSize = mFuncNum * 2 * mPtrSize + mPtrSize;
            uint32_t fileOffset = mConverter(*(uint32_t *) (mFuncTable + funcTableSize));

            mFileTable = buffer + fileOffset;
            mFileNum = mConverter(*(uint32_t *) mFileTable);

            break;
        }

        case VERSION116: {
            mFuncNum = mConverter(buffer + 8, mPtrSize);
            mFileNum = mConverter(buffer + 8 + mPtrSize, mPtrSize);

            mFuncNameTable = buffer + mConverter(buffer + 8 + 2 * mPtrSize, mPtrSize);
            mCuTable = buffer + mConverter(buffer + 8 + 3 * mPtrSize, mPtrSize);
            mFileTable = buffer + mConverter(buffer + 8 + 4 * mPtrSize, mPtrSize);
            mPCTable = buffer + mConverter(buffer + 8 + 5 * mPtrSize, mPtrSize);
            mFuncData = buffer + mConverter(buffer + 8 + 6 * mPtrSize, mPtrSize);
            mFuncTable = buffer + mConverter(buffer + 8 + 6 * mPtrSize, mPtrSize);

            break;
        }

        case VERSION118:
        case VERSION120: {
            mFuncNum = mConverter(buffer + 8, mPtrSize);
            mFileNum = mConverter(buffer + 8 + mPtrSize, mPtrSize);

            mBase += mConverter(buffer + 8 + 2 * mPtrSize, mPtrSize);

            mFuncNameTable = buffer + mConverter(buffer + 8 + 3 * mPtrSize, mPtrSize);
            mCuTable = buffer + mConverter(buffer + 8 + 4 * mPtrSize, mPtrSize);
            mFileTable = buffer + mConverter(buffer + 8 + 5 * mPtrSize, mPtrSize);
            mPCTable = buffer + mConverter(buffer + 8 + 6 * mPtrSize, mPtrSize);
            mFuncData = buffer + mConverter(buffer + 8 + 7 * mPtrSize, mPtrSize);
            mFuncTable = buffer + mConverter(buffer + 8 + 7 * mPtrSize, mPtrSize);

            break;
        }
    }
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
    return {this, mFuncTable};
}

go::symbol::SymbolIterator go::symbol::SymbolTable::end() const {
    return begin() + mFuncNum;
}

const std::byte *go::symbol::SymbolTable::data() const {
    size_t index = mMemoryBuffer.index();

    if (index == 0) {
        return std::get<std::shared_ptr<elf::ISection>>(mMemoryBuffer)->data();
    } else if (index == 1) {
        return std::get<std::unique_ptr<std::byte[]>>(mMemoryBuffer).get();
    }

    return std::get<const std::byte *>(mMemoryBuffer);
}

go::symbol::Symbol::Symbol(const go::symbol::SymbolTable *table, const std::byte *buffer)
        : mTable(table), mBuffer(buffer) {

}

uint64_t go::symbol::Symbol::entry() const {
    if (mTable->mVersion < VERSION118)
        return mTable->mBase + mTable->mConverter(mBuffer, mTable->mPtrSize);

    return mTable->mBase + mTable->mConverter(*(uint32_t *) mBuffer);
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

        return (const char *) mTable->mFuncData + mTable->mConverter((mTable->mFileTable + n * 4), sizeof(int));
    }

    uint32_t offset = mTable->mConverter((mTable->mCuTable + (field(8) + n) * 4), sizeof(uint32_t));

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
    return mTable->mConverter(
            *(uint32_t *) (mBuffer + (mTable->mVersion >= VERSION118 ? 4 : mTable->mPtrSize) + (n - 1) * 4)
    );
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

go::symbol::seek::SymbolTable::SymbolTable(
        SymbolVersion version,
        endian::Converter converter,
        std::ifstream stream,
        std::streamoff offset,
        uint64_t address,
        uint64_t base
) : mVersion(version), mConverter(converter), mStream(std::move(stream)), mOffset(offset), mAddress(address),
    mBase(base) {
    std::byte buffer[128];

    mStream.seekg(mOffset, std::ifstream::beg);
    mStream.read((char *) buffer, sizeof(buffer));

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

            mStream.seekg(mOffset + (std::streamoff) (mFuncTable + funcTableSize - mAddress), std::ifstream::beg);
            mStream.read((char *) &fileOffset, sizeof(uint32_t));
            fileOffset = mConverter(fileOffset);

            mFileTable = mAddress + fileOffset;

            mStream.seekg(mOffset + (std::streamoff) (mFileTable - mAddress), std::ifstream::beg);
            mStream.read((char *) &mFileNum, sizeof(uint32_t));

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

    mStream.seekg(mOffset + (std::streamoff) (mFuncTable - mAddress), std::ifstream::beg);
    mStream.read((char *) mFuncTableBuffer.get(), (std::streamsize) size);
}

go::symbol::seek::SymbolIterator go::symbol::seek::SymbolTable::find(uint64_t address) {
    if (address < operator[](0).entry() || address >= operator[](mFuncNum).entry())
        return end();

    return std::upper_bound(begin(), end() + 1, address, [](uint64_t value, const auto &entry) {
        return value < entry.entry();
    }) - 1;
}

go::symbol::seek::SymbolIterator go::symbol::seek::SymbolTable::find(std::string_view name) {
    return std::find_if(begin(), end(), [=](const auto &entry) {
        return name == entry.symbol().name();
    });
}

size_t go::symbol::seek::SymbolTable::size() const {
    return mFuncNum;
}

go::symbol::seek::SymbolEntry go::symbol::seek::SymbolTable::operator[](size_t index) {
    return *(begin() + std::ptrdiff_t(index));
}

go::symbol::seek::SymbolIterator go::symbol::seek::SymbolTable::begin() {
    return {this, mFuncTableBuffer.get()};
}

go::symbol::seek::SymbolIterator go::symbol::seek::SymbolTable::end() {
    return begin() + mFuncNum;
}

go::symbol::seek::Symbol::Symbol(go::symbol::seek::SymbolTable *table, uint64_t address)
        : mTable(table), mAddress(address) {

}

uint64_t go::symbol::seek::Symbol::entry() const {
    if (mTable->mVersion < VERSION118) {
        std::byte buffer[8] = {};

        mTable->mStream.seekg(mTable->mOffset + (std::streamoff) (mAddress - mTable->mAddress), std::ifstream::beg);
        mTable->mStream.read((char *) &buffer, mTable->mPtrSize);

        if (mTable->mPtrSize == 4)
            return mTable->mBase + mTable->mConverter(*(uint32_t *) buffer);

        return mTable->mBase + mTable->mConverter(*(uint64_t *) buffer);
    }

    uint32_t entry;

    mTable->mStream.seekg(mTable->mOffset + (std::streamoff) (mAddress - mTable->mAddress), std::ifstream::beg);
    mTable->mStream.read((char *) &entry, sizeof(uint32_t));

    return mTable->mBase + mTable->mConverter(entry);
}

std::string go::symbol::seek::Symbol::name() const {
    mTable->mStream.seekg(
            mTable->mOffset + (std::streamoff) (mTable->mFuncNameTable + field(1) - mTable->mAddress),
            std::ifstream::beg
    );

    std::string name;
    std::getline(mTable->mStream, name, '\0');

    return name;
}

int go::symbol::seek::Symbol::frameSize(uint64_t pc) const {
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

int go::symbol::seek::Symbol::sourceLine(uint64_t pc) const {
    return value(field(6), entry(), pc);
}

std::string go::symbol::seek::Symbol::sourceFile(uint64_t pc) const {
    int n = value(field(5), entry(), pc);

    if (n < 0 || n > mTable->mFileNum)
        return "";

    if (mTable->mVersion == VERSION12) {
        if (n == 0)
            return "";

        int offset;

        mTable->mStream.seekg(
                mTable->mOffset + (std::streamoff) (mTable->mFileTable + n * 4 - mTable->mAddress),
                std::ifstream::beg
        );

        mTable->mStream.read((char *) &offset, sizeof(int));
        offset = mTable->mConverter(offset);

        mTable->mStream.seekg(
                mTable->mOffset + (std::streamoff) (mTable->mFuncData + offset - mTable->mAddress),
                std::ifstream::beg
        );

        std::string name;
        std::getline(mTable->mStream, name, '\0');

        return name;
    }

    uint32_t offset;

    mTable->mStream.seekg(
            mTable->mOffset + (std::streamoff) (mTable->mCuTable + (field(8) + n) * 4 - mTable->mAddress),
            std::ifstream::beg
    );

    mTable->mStream.read((char *) &offset, sizeof(uint32_t));
    offset = mTable->mConverter(offset);

    if (!offset)
        return "";

    mTable->mStream.seekg(
            mTable->mOffset + (std::streamoff) (mTable->mFileTable + offset - mTable->mAddress),
            std::ifstream::beg
    );

    std::string name;
    std::getline(mTable->mStream, name, '\0');

    return name;
}

bool go::symbol::seek::Symbol::isStackTop() const {
    return std::any_of(STACK_TOP_FUNCTION.begin(), STACK_TOP_FUNCTION.end(), [name = name()](const auto &func) {
        return name == func;
    });
}

uint32_t go::symbol::seek::Symbol::field(int n) const {
    mTable->mStream.seekg(
            mTable->mOffset +
            (std::streamoff) (mAddress + (mTable->mVersion >= VERSION118 ? 4 : mTable->mPtrSize) + (n - 1) * 4 -
                              mTable->mAddress),
            std::ifstream::beg
    );

    uint32_t value;
    mTable->mStream.read((char *) &value, sizeof(uint32_t));

    return mTable->mConverter(value);
}

int go::symbol::seek::Symbol::value(uint32_t offset, uint64_t entry, uint64_t target) const {
    mTable->mStream.seekg(
            mTable->mOffset + (std::streamoff) (mTable->mPCTable + offset - mTable->mAddress),
            std::ifstream::beg
    );

    int length = 0;
    std::byte buffer[1024];

    mTable->mStream.read((char *) buffer, sizeof(buffer));

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
        mTable->mStream.read((char *) buffer + sizeof(buffer) - length, length);

        length = 0;
    }

    return value;
}

go::symbol::seek::SymbolEntry::SymbolEntry(go::symbol::seek::SymbolTable *table, uint64_t entry, uint64_t offset)
        : mTable(table), mEntry(entry), mOffset(offset) {

}

uint64_t go::symbol::seek::SymbolEntry::entry() const {
    return mEntry;
}

go::symbol::seek::Symbol go::symbol::seek::SymbolEntry::symbol() const {
    return {mTable, mTable->mFuncData + mOffset};
}

go::symbol::seek::SymbolIterator::SymbolIterator(go::symbol::seek::SymbolTable *table, const std::byte *buffer)
        : mTable(table), mBuffer(buffer), mSize(table->mVersion >= VERSION118 ? 4 : table->mPtrSize) {

}

go::symbol::seek::SymbolEntry go::symbol::seek::SymbolIterator::operator*() {
    return {
            mTable,
            mTable->mBase + mTable->mConverter(mBuffer, mSize),
            mTable->mConverter(mBuffer + mSize, mSize)
    };
}

go::symbol::seek::SymbolIterator &go::symbol::seek::SymbolIterator::operator--() {
    mBuffer -= 2 * mSize;
    return *this;
}

go::symbol::seek::SymbolIterator &go::symbol::seek::SymbolIterator::operator++() {
    mBuffer += 2 * mSize;
    return *this;
}

go::symbol::seek::SymbolIterator &go::symbol::seek::SymbolIterator::operator+=(std::ptrdiff_t offset) {
    mBuffer += offset * 2 * mSize;
    return *this;
}

go::symbol::seek::SymbolIterator go::symbol::seek::SymbolIterator::operator-(std::ptrdiff_t offset) {
    return {mTable, mBuffer - offset * 2 * mSize};
}

go::symbol::seek::SymbolIterator go::symbol::seek::SymbolIterator::operator+(std::ptrdiff_t offset) {
    return {mTable, mBuffer + offset * 2 * mSize};
}

bool go::symbol::seek::SymbolIterator::operator==(const go::symbol::seek::SymbolIterator &rhs) {
    return mBuffer == rhs.mBuffer;
}

bool go::symbol::seek::SymbolIterator::operator!=(const go::symbol::seek::SymbolIterator &rhs) {
    return !operator==(rhs);
}

std::ptrdiff_t go::symbol::seek::SymbolIterator::operator-(const go::symbol::seek::SymbolIterator &rhs) {
    return (mBuffer - rhs.mBuffer) / std::ptrdiff_t(2 * mSize);
}