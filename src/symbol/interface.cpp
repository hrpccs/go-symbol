#include <go/symbol/interface.h>
#include <go/binary.h>

go::symbol::InterfaceTable::InterfaceTable(
        elf::Reader reader,
        std::shared_ptr<elf::ISection> section,
        Version version,
        uint64_t types,
        uint64_t base,
        size_t ptrSize,
        endian::Converter converter
) : mReader(std::move(reader)), mSection(std::move(section)), mVersion(version), mTypes(types), mBase(base),
    mPtrSize(ptrSize), mConverter(converter) {

}

size_t go::symbol::InterfaceTable::size() const {
    return mSection->size() / mPtrSize;
}

go::symbol::Interface go::symbol::InterfaceTable::operator[](size_t index) const {
    return *(begin() + std::ptrdiff_t(index));
}

go::symbol::InterfaceIterator go::symbol::InterfaceTable::begin() const {
    return {this, mSection->data()};
}

go::symbol::InterfaceIterator go::symbol::InterfaceTable::end() const {
    return begin() + std::ptrdiff_t(size());
}

go::symbol::InterfaceIterator::InterfaceIterator(const go::symbol::InterfaceTable *table, const std::byte *buffer)
        : mTable(table), mBuffer(buffer) {

}

go::symbol::Interface::Interface(const go::symbol::InterfaceTable *table, uint64_t address)
        : mTable(table), mAddress(address) {

}

uint64_t go::symbol::Interface::address() const {
    return mAddress + mTable->mBase;
}

uint64_t go::symbol::Interface::methodCount() const {
    const std::byte *buffer = mTable->mReader.virtualMemory(mAddress);

    if (!buffer)
        return 0;

    buffer = mTable->mReader.virtualMemory(mTable->mConverter(buffer, mTable->mPtrSize));

    if (!buffer)
        return 0;

    return mTable->mConverter(buffer + (mTable->mPtrSize == 8 ? 64 : 40), mTable->mPtrSize);
}

uint64_t go::symbol::Interface::method(uint64_t index) const {
    const std::byte *buffer = mTable->mReader.virtualMemory(mAddress);

    if (!buffer)
        return 0;

    return mTable->mConverter(buffer + (mTable->mPtrSize == 8 ? 24 : 16) + index * mTable->mPtrSize, mTable->mPtrSize);
}

std::optional<std::string> go::symbol::Interface::name() const {
    const std::byte *buffer = mTable->mReader.virtualMemory(mAddress + mTable->mPtrSize);

    if (!buffer)
        return std::nullopt;

    return typeName(buffer);
}

std::optional<std::string> go::symbol::Interface::interfaceName() const {
    const std::byte *buffer = mTable->mReader.virtualMemory(mAddress);

    if (!buffer)
        return std::nullopt;

    return typeName(buffer);
}

std::optional<std::string> go::symbol::Interface::typeName(const std::byte *buffer) const {
    buffer = mTable->mReader.virtualMemory(mTable->mConverter(buffer, mTable->mPtrSize));

    if (!buffer)
        return std::nullopt;

    uint64_t offset = mTable->mConverter(buffer + (mTable->mPtrSize == 8 ? 40 : 24), 4);

    if (!offset)
        return std::nullopt;

    buffer = mTable->mReader.virtualMemory(mTable->mTypes + offset);

    if (!buffer)
        return std::nullopt;

    if (mTable->mVersion <= Version{1, 16})
        return std::string{
                (const char *) buffer + 3,
                std::to_integer<size_t>(buffer[1]) << 8 | std::to_integer<size_t>(buffer[2])
        };

    std::optional<std::pair<int64_t, int>> result = go::binary::uVarInt(buffer + 1);

    if (!result)
        return std::nullopt;

    return std::string{(const char *) buffer + 1 + result->second, (size_t) result->first};
}

go::symbol::Interface go::symbol::InterfaceIterator::operator*() {
    return {mTable, mTable->mConverter(mBuffer, mTable->mPtrSize)};
}

go::symbol::InterfaceIterator &go::symbol::InterfaceIterator::operator--() {
    mBuffer -= mTable->mPtrSize;
    return *this;
}

go::symbol::InterfaceIterator &go::symbol::InterfaceIterator::operator++() {
    mBuffer += mTable->mPtrSize;
    return *this;
}

go::symbol::InterfaceIterator &go::symbol::InterfaceIterator::operator+=(std::ptrdiff_t offset) {
    mBuffer += offset * mTable->mPtrSize;
    return *this;
}

go::symbol::InterfaceIterator go::symbol::InterfaceIterator::operator-(std::ptrdiff_t offset) {
    return {mTable, mBuffer - offset * mTable->mPtrSize};
}

go::symbol::InterfaceIterator go::symbol::InterfaceIterator::operator+(std::ptrdiff_t offset) {
    return {mTable, mBuffer + offset * mTable->mPtrSize};
}

bool go::symbol::InterfaceIterator::operator==(const go::symbol::InterfaceIterator &rhs) {
    return mBuffer == rhs.mBuffer;
}

bool go::symbol::InterfaceIterator::operator!=(const go::symbol::InterfaceIterator &rhs) {
    return !operator==(rhs);
}

std::ptrdiff_t go::symbol::InterfaceIterator::operator-(const go::symbol::InterfaceIterator &rhs) {
    return (mBuffer - rhs.mBuffer) / std::ptrdiff_t(mTable->mPtrSize);
}
