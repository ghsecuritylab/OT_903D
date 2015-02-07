

#ifndef FormData_h
#define FormData_h

#include "PlatformString.h"
#include <wtf/RefCounted.h>
#include <wtf/Vector.h>

namespace WebCore {

class ChromeClient;

class FormDataElement {
public:
    FormDataElement() : m_type(data) { }
    FormDataElement(const Vector<char>& array) : m_type(data), m_data(array) { }
    FormDataElement(const String& filename, bool shouldGenerateFile) : m_type(encodedFile), m_filename(filename), m_shouldGenerateFile(shouldGenerateFile) { }

    enum { data, encodedFile } m_type;
    Vector<char> m_data;
    String m_filename;
    String m_generatedFilename;
    bool m_shouldGenerateFile;
};

inline bool operator==(const FormDataElement& a, const FormDataElement& b)
{
    if (&a == &b)
        return true;
    
    if (a.m_type != b.m_type)
        return false;
    if (a.m_data != b.m_data)
        return false;
    if (a.m_filename != b.m_filename)
        return false;

    return true;
}
 
inline bool operator!=(const FormDataElement& a, const FormDataElement& b)
{
    return !(a == b);
}
 
class FormData : public RefCounted<FormData> {
public:
    static PassRefPtr<FormData> create();
    static PassRefPtr<FormData> create(const void*, size_t);
    static PassRefPtr<FormData> create(const CString&);
    static PassRefPtr<FormData> create(const Vector<char>&);
    PassRefPtr<FormData> copy() const;
    PassRefPtr<FormData> deepCopy() const;
    ~FormData();
    
    void appendData(const void* data, size_t);
    void appendFile(const String& filename, bool shouldGenerateFile = false);

    void flatten(Vector<char>&) const; // omits files
    String flattenToString() const; // omits files

    bool isEmpty() const { return m_elements.isEmpty(); }
    const Vector<FormDataElement>& elements() const { return m_elements; }

    void generateFiles(ChromeClient*);
    void removeGeneratedFilesIfNeeded();

    bool alwaysStream() const { return m_alwaysStream; }
    void setAlwaysStream(bool alwaysStream) { m_alwaysStream = alwaysStream; }

    // Identifies a particular form submission instance.  A value of 0 is used
    // to indicate an unspecified identifier.
    void setIdentifier(int64_t identifier) { m_identifier = identifier; }
    int64_t identifier() const { return m_identifier; }

private:
    FormData();
    FormData(const FormData&);

    Vector<FormDataElement> m_elements;
    int64_t m_identifier;
    bool m_hasGeneratedFiles;
    bool m_alwaysStream;
};

inline bool operator==(const FormData& a, const FormData& b)
{
    return a.elements() == b.elements();
}

inline bool operator!=(const FormData& a, const FormData& b)
{
    return a.elements() != b.elements();
}

} // namespace WebCore

#endif