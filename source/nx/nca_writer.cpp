/*
Copyright (c) 2017-2018 Adubbz

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#include "nx/nca_writer.h"
#include "util/error.hpp"
#include <zstd.h>
#include <string.h>
#include <memory>
#include "util/crypto.hpp"
#include "util/config.hpp"
#include "util/title_util.hpp"
#include "install/nca.hpp"
#include <limits>

void append(std::vector<u8>& buffer, const u8* ptr, u64 sz)
{
     u64 offset = buffer.size();
     buffer.resize(offset + sz);
     memcpy(buffer.data() + offset, ptr, sz);
}

// Wrapper over AES128-CTR to handle seek+encrypt/decrypt
class Aes128CtrCipher
{
public:
     // NOTE: Switch is Little Endian so we byte-swap here for convenience
     Aes128CtrCipher(const u8* key, const u8* counter)
     : crypto(key, Crypto::AesCtr(Crypto::swapEndian(((const u64*)counter)[0])))
     {
     }

     void decrypt(void* p, u64 sz, u64 offset)
     {
          crypto.seek(offset);
          crypto.decrypt(p, p, sz);
     }

     void encrypt(void* p, u64 sz, u64 offset)
     {
          crypto.seek(offset);
          crypto.encrypt(p, p, sz);
     }

     Crypto::Aes128Ctr crypto; // Counter (Ctr) mode, for streaming
};

// region Header Structs

// NCZSECTN Section Header structure
struct NczSectionHeader
{
     u64 offset;
     u64 size;
     u8 cryptoType;
     u8 padding1[7];
     u64 padding2;
     u8 cryptoKey[0x10];
     u8 cryptoCounter[0x10];
} NX_PACKED;

// NCZSECTN Body Header structure
class NczHeader
{
public:
     static const u64 MAGIC = 0x4E544345535A434E;
     static constexpr size_t MIN_HEADER_SIZE = sizeof(u64) * 2; // magic + sectionCount

     const bool isValid()
     {
          return m_magic == MAGIC && m_sectionCount < 0xFFFF;
     }

     const u64 size() const
     {
          return sizeof(m_magic) + sizeof(m_sectionCount) + sizeof(NczSectionHeader) * m_sectionCount;
     }

     const NczSectionHeader& section(u64 i) const
     {
          return m_sections[i];
     }

     const u64 sectionCount() const
     {
          return m_sectionCount;
     }

protected:
     u64 m_magic;
     u64 m_sectionCount;
     NczSectionHeader m_sections[1];
} NX_PACKED;

// endregion

// region NcaBodyWriter methods

NcaBodyWriter::NcaBodyWriter(const NcmContentId& ncaId, u64 offset, std::shared_ptr<nx::ncm::ContentStorage>& contentStorage)
: m_contentStorage(contentStorage), m_ncaId(ncaId), m_offset(offset)
{
     // Pre-allocate the memory to our buffer size to prevent reallocations/fragmentation
     m_contentBuffer.reserve(CONTENT_BUFFER_SIZE);
}

NcaBodyWriter::~NcaBodyWriter()
{
     NcaBodyWriter::close();
}

void NcaBodyWriter::close()
{
     if (isClosed()) return; // Idempotent close

     doBeforeClose(); // Derived close/flush delegates so content buffer is complete

     flushContentBuffer(); // Virtual dispatch - overrides expected to invoke parent

     doClose(); // Derived cleanup before finalizing close

     // Free resources
     m_contentBuffer.clear(); // reclaim ok
     m_contentStorage = NULL;

     CloseableWriter::close(); // Mark as closed after all cleanups are done
}

void NcaBodyWriter::write(const  u8* ptr, u64 sz)
{
     if (isClosed()) {
          LOG_DEBUG("write() called on closed NcaBodyWriter");
          return;
     }

     if (!sz) return; // no data

     while (sz)
     {
          if (m_contentBuffer.size() < CONTENT_BUFFER_SIZE)
          {
               const u64 remainder = std::min(sz, CONTENT_BUFFER_SIZE - (u64)m_contentBuffer.size());
               append(m_contentBuffer, ptr, remainder);
               ptr += remainder;
               sz -= remainder;
          }

          if (m_contentBuffer.size() < CONTENT_BUFFER_SIZE)
          {
               // assert sz == 0
               return; // Need more data
          }

          // assert m_contentBuffer.size() == CONTENT_BUFFER_SIZE

          flushContentBuffer();
     }
}

void NcaBodyWriter::flushContentBuffer()
{
     if (isClosed()) {
          LOG_DEBUG("flushContentBuffer() called on closed NcaBodyWriter");
          return;
     }

     if (m_contentBuffer.empty()) return; // No data

     if (m_contentStorage)
     {
          m_contentStorage->WritePlaceholder(*(NcmPlaceHolderId*)&m_ncaId, m_offset, m_contentBuffer.data(), m_contentBuffer.size());
          m_offset += m_contentBuffer.size();
     }

     m_contentBuffer.resize(0);
}

// endregion

class NczBodyWriter : public NcaBodyWriter
{
public:
     static const u64 NCZ_BODY_CHUNK_SIZE = 0x1000000; // 16MB

     NczBodyWriter(const NcmContentId& ncaId, u64 offset, std::shared_ptr<nx::ncm::ContentStorage>& contentStorage) : NcaBodyWriter(ncaId, offset, contentStorage)
     {
          buffIn = malloc(buffInSize);
          buffOut = malloc(buffOutSize);

          dctx = ZSTD_createDCtx();
     }

     ~NczBodyWriter() override
     {
          NcaBodyWriter::close();
     }

protected:

     void doClose() override
     {
          // Free resources
          currentSectionCipher.reset(); // unique_ptr handles delete
          currentSectionIdx = (u64)-1;
          sections.clear(); // reclaim ok

          if (dctx)
          {
               ZSTD_freeDCtx(dctx);
               dctx = NULL;
          }
     }

     void doBeforeClose() override
     {
          // Handle dangling buffer < NCZ_BODY_CHUNK_SIZE
          if (this->m_buffer.size())
          {
               processChunk(m_buffer.data(), m_buffer.size());
               m_buffer.clear(); // reclaim ok
          }
     }

private:

     // Find the section index for the specified offset
     // Returns -1 if none found
     int getSectionIndexForOffset(u64 offset)
     {
          for (int i = 0; i < sections.size(); i++)
          {
               if (offset >= sections[i].offset && offset < sections[i].offset + sections[i].size)
               {
                    return i;
               }
          }
          return -1;
     }

     // Find the next closest section index for the specified offset
     // Returns -1 if none found
     int getNextSectionIndexForOffset(u64 offset)
     {
          int nextSectionIdx = -1;
          u64 nextSectionOffset = std::numeric_limits<u64>::max();
          for (int i = 0; i < sections.size(); i++)
          {
               if (sections[i].offset > offset && sections[i].offset < nextSectionOffset)
               {
                    nextSectionIdx = i;
                    nextSectionOffset = sections[i].offset;
               }
          }
          return nextSectionIdx;
     }

     bool encrypt(const u8* ptr, u64 sz, u64 offset)
     {
          while (sz)
          {
               int offsetSectionIdx = getSectionIndexForOffset(offset);
               u64 chunk = sz;

               if (offsetSectionIdx >= 0)
               {
                    NczSectionHeader* offsetSection = &sections[offsetSectionIdx];

                    // Create new context if section changed
                    if (offsetSectionIdx != currentSectionIdx) // -1 => true
                    {
                         currentSectionCipher.reset(); // Delete early as we may not re-assign
                         if (offsetSection->cryptoType == 3)
                         {
                              currentSectionCipher = std::make_unique<Aes128CtrCipher>(offsetSection->cryptoKey, offsetSection->cryptoCounter);
                         }
                         currentSectionIdx = offsetSectionIdx;
                    }

                    const u64 sectionEnd = offsetSection->offset + offsetSection->size;

                    // assert offset < sectionEnd

                    chunk = std::min<u64>(sz, sectionEnd - offset);

                    // assert chunk > 0

                    if (currentSectionCipher)
                    {
                         currentSectionCipher->encrypt((void*)ptr, chunk, offset);
                    }
               }
               else
               {
                    // When an offset doesn't fall within a defined section,
                    // (i.e. Its not encrypted - Possibly padding),
                    // We look for the next closest section to determine
                    // how many unencrypted bytes to account for
                    int nextSectionIdx = getNextSectionIndexForOffset(offset);
                    if (nextSectionIdx >= 0)
                    {
                         NczSectionHeader* nextSection = &sections[nextSectionIdx];
                         u64 nextSectionStart = nextSection->offset;

                         // assert offset < nextSectionStart

                         chunk = std::min<u64>(sz, nextSectionStart - offset);
                    }
               }

               // assert chunk > 0

               offset += chunk;
               ptr += chunk;
               sz -= chunk;
          }

          return true;
     }

protected:

     void flushContentBuffer() override
     {
          if (isClosed()) {
               LOG_DEBUG("flushContentBuffer() called on closed NczBodyWriter");
               return;
          }

          const u64 encryptOffset = m_offset;
          const u64 encryptSize = m_contentBuffer.size();

          if (encryptSize)
          {
               // Apply section-based encryption in-place before flushing
               encrypt(m_contentBuffer.data(), encryptSize, encryptOffset);
          }

          NcaBodyWriter::flushContentBuffer();
     }

     u64 processChunk(const u8* ptr, u64 sz)
     {
          while(sz > 0)
          {
               const size_t readChunkSz = std::min(sz, buffInSize);
               ZSTD_inBuffer input = { ptr, readChunkSz, 0 };

               while(input.pos < input.size)
               {
                    ZSTD_outBuffer output = { buffOut, buffOutSize, 0 };
                    size_t const ret = ZSTD_decompressStream(dctx, &output, &input);

                    if (ZSTD_isError(ret))
                    {
                         LOG_DEBUG("%s\n", ZSTD_getErrorName(ret));
                         return 0;
                    }

                    if (output.pos > 0)
                    {
                         NcaBodyWriter::write((u8*)buffOut, output.pos);
                    }
               }

               sz -= readChunkSz;
               ptr += readChunkSz;
          }

          return 1;
     }

public:

     void write(const  u8* ptr, u64 sz) override
     {
          if (isClosed()) {
               LOG_DEBUG("write() called on closed NczBodyWriter");
               return;
          }

          if (!sz) return; // no data

          if (!m_sectionsInitialized)
          {
               // Need to buffer enough to get the section count
               // to compute the total size of the header.
               if (m_buffer.size() < NczHeader::MIN_HEADER_SIZE)
               {
                    const u64 remainder = std::min(sz, NczHeader::MIN_HEADER_SIZE - m_buffer.size());
                    append(m_buffer, ptr, remainder);
                    ptr += remainder;
                    sz -= remainder;
               }

               if (m_buffer.size() < NczHeader::MIN_HEADER_SIZE)
               {
                    // assert sz == 0
                    return;
               }

               // assert m_buffer.size() >= NczHeader::MIN_HEADER_SIZE

               auto header = (NczHeader*)m_buffer.data();
               if (!header->isValid())
               {
                    THROW_FORMAT("Invalid NCZ Header");
               }

               const u64 header_size = header->size(); // Compute once

               // Need to buffer the rest of the header before
               // we can extract the sections
               if (m_buffer.size() < header_size)
               {
                    const u64 remainder = std::min(sz, header_size - m_buffer.size());
                    append(m_buffer, ptr, remainder);
                    ptr += remainder;
                    sz -= remainder;
               }

               if (m_buffer.size() < header_size)
               {
                    // assert sz == 0
                    return;
               }

               // assert m_buffer.size() == header_size

               // Now we can initialize the sections
               header = (NczHeader*)m_buffer.data();

               for (u64 i = 0; i < header->sectionCount(); i++)
               {
                    sections.push_back(header->section(i));
               }

               m_sectionsInitialized = true;
               m_buffer.resize(0);
          }

          while (sz)
          {
               // Need to buffer each chunk before processing
               if (m_buffer.size() < NCZ_BODY_CHUNK_SIZE)
               {
                    const u64 remainder = std::min(sz, NCZ_BODY_CHUNK_SIZE - m_buffer.size());
                    append(m_buffer, ptr, remainder);
                    ptr += remainder;
                    sz -= remainder;
               }

               if (m_buffer.size() == NCZ_BODY_CHUNK_SIZE)
               {
                    processChunk(m_buffer.data(), m_buffer.size());
                    m_buffer.resize(0);
               }
          }
     }

private:
     size_t const buffInSize = ZSTD_DStreamInSize();
     size_t const buffOutSize = ZSTD_DStreamOutSize();

     void* buffIn = NULL;
     void* buffOut = NULL;

     ZSTD_DCtx* dctx = NULL;

     std::vector<u8> m_buffer;

     bool m_sectionsInitialized = false;

     std::vector<NczSectionHeader> sections;
     std::unique_ptr<Aes128CtrCipher> currentSectionCipher; // Crypto cipher for current section
     u64 currentSectionIdx = (u64)-1; // Track which section the cipher is for
};

NcaWriter::NcaWriter(const NcmContentId& ncaId, std::shared_ptr<nx::ncm::ContentStorage>& contentStorage) : m_ncaId(ncaId), m_contentStorage(contentStorage), m_writer(NULL)
{
}

NcaWriter::~NcaWriter()
{
     NcaWriter::close();
}

void NcaWriter::close()
{
     if (isClosed()) return; // Idempotent close

     if (m_writer)
     {
          m_writer->close();
          m_writer = NULL;
     }
     else if (!m_buffer.empty())
     {
          if (m_contentStorage)
          {
              flushHeader();
          }

          m_buffer.clear(); // reclaim ok
     }
     m_contentStorage = NULL;

     CloseableWriter::close(); // Mark as closed after all cleanups are done
}

void NcaWriter::write(const  u8* ptr, u64 sz)
{
     if (isClosed()) {
          LOG_DEBUG("write() called on closed NcaWriter");
          return;
     }

     if (!sz) return; // no data

     if (!m_headerFlushed)
     {
          // Need to buffer the full header
          // before we can flush it
          if (m_buffer.size() < NCA_HEADER_SIZE)
          {
               const u64 remainder = std::min(sz, NCA_HEADER_SIZE - m_buffer.size());
               append(m_buffer, ptr, remainder);

               ptr += remainder;
               sz -= remainder;
          }

          if (m_buffer.size() < NCA_HEADER_SIZE)
          {
               // assert sz == 0
               return;
          }

          // assert m_buffer.size() == NCA_HEADER_SIZE

          // Now we can flush the header
          flushHeader();
          m_headerFlushed = true;
          m_buffer.resize(0);
     }

     if (!sz) return; // no data

     if (!m_writer)
     {
          const u64 header_size = sizeof(NczHeader::MAGIC);

          // Need to buffer enough to identify magic headers
          if (m_buffer.size() < header_size)
          {
               const u64 remainder = std::min(sz, header_size - m_buffer.size());
               append(m_buffer, ptr, remainder);

               ptr += remainder;
               sz -= remainder;
          }

          if (m_buffer.size() < header_size) {
               // assert sz == 0
               return;
          }

          // assert m_buffer.size() == header_size

          u64 magic = *(u64*)m_buffer.data();

          if (magic == NczHeader::MAGIC)
          {
               // NOTE: Don't clear header, it needs to be written to m_write for downstream consumption
               m_writer = std::make_shared<NczBodyWriter>(m_ncaId, NCA_HEADER_SIZE, m_contentStorage);
          }
          else
          {
               m_writer = std::make_shared<NcaBodyWriter>(m_ncaId, NCA_HEADER_SIZE, m_contentStorage);
          }

          // assert !m_buffer.empty()

          // Flush buffer now because
          // future writes will go directly to m_writer
          m_writer->write(m_buffer.data(), m_buffer.size());
          m_buffer.clear(); // reclaim ok
     }

     // assert m_writer
     // assert buffer.empty()

     if (!sz) return; // no data

     m_writer->write(ptr, sz);
}

void NcaWriter::flushHeader()
{
     if (isClosed()) {
          LOG_DEBUG("flushHeader() called on closed NcaWriter");
          return;
     }

     tin::install::NcaHeader header;

     if (m_buffer.size() < sizeof(header))
     {
          LOG_DEBUG("Insufficient data to flush NCA header");
          return;
     }

     memcpy(&header, m_buffer.data(), sizeof(header));
     Crypto::AesXtr decryptor(Crypto::Keys().headerKey, false);
     Crypto::AesXtr encryptor(Crypto::Keys().headerKey, true);
     decryptor.decrypt(&header, &header, sizeof(header), 0, 0x200);

     if (header.magic == MAGIC_NCA3)
     {
          if (m_contentStorage)
          {
               m_contentStorage->CreatePlaceholder(m_ncaId, *(NcmPlaceHolderId*)&m_ncaId, header.nca_size);
          }
     }
     else
     {
          THROW_FORMAT("Invalid NCA magic");
     }

     if (header.distribution == 1)
     {
          header.distribution = 0;
     }
     encryptor.encrypt(m_buffer.data(), &header, sizeof(header), 0, 0x200);

     if (m_contentStorage)
     {
          m_contentStorage->WritePlaceholder(*(NcmPlaceHolderId*)&m_ncaId, 0, m_buffer.data(), m_buffer.size());
     }
}
