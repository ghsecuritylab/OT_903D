

#ifndef RenderTableSection_h
#define RenderTableSection_h

#include "RenderTable.h"
#include <wtf/Vector.h>

namespace WebCore {

class RenderTableCell;
class RenderTableRow;

class RenderTableSection : public RenderBox {
public:
    RenderTableSection(Node*);
    virtual ~RenderTableSection();

    const RenderObjectChildList* children() const { return &m_children; }
    RenderObjectChildList* children() { return &m_children; }

    virtual void addChild(RenderObject* child, RenderObject* beforeChild = 0);

    virtual int firstLineBoxBaseline() const;

    void addCell(RenderTableCell*, RenderTableRow* row);

    void setCellWidths();
    int calcRowHeight();
    int layoutRows(int height);

    RenderTable* table() const { return toRenderTable(parent()); }

    struct CellStruct {
        RenderTableCell* cell;
        bool inColSpan; // true for columns after the first in a colspan
    };

    typedef Vector<CellStruct> Row;

    struct RowStruct {
        Row* row;
        RenderTableRow* rowRenderer;
        int baseline;
        Length height;
    };

    CellStruct& cellAt(int row,  int col) { return (*m_grid[row].row)[col]; }
    const CellStruct& cellAt(int row, int col) const { return (*m_grid[row].row)[col]; }

    void appendColumn(int pos);
    void splitColumn(int pos, int newSize);

    int calcOuterBorderTop() const;
    int calcOuterBorderBottom() const;
    int calcOuterBorderLeft(bool rtl) const;
    int calcOuterBorderRight(bool rtl) const;
    void recalcOuterBorder();

    int outerBorderTop() const { return m_outerBorderTop; }
    int outerBorderBottom() const { return m_outerBorderBottom; }
    int outerBorderLeft() const { return m_outerBorderLeft; }
    int outerBorderRight() const { return m_outerBorderRight; }

    int numRows() const { return m_gridRows; }
    int numColumns() const;
    void recalcCells();
    void recalcCellsIfNeeded()
    {
        if (m_needsCellRecalc)
            recalcCells();
    }

    bool needsCellRecalc() const { return m_needsCellRecalc; }
    void setNeedsCellRecalc()
    {
        m_needsCellRecalc = true;
        table()->setNeedsSectionRecalc();
    }

    int getBaseline(int row) { return m_grid[row].baseline; }

private:
    virtual RenderObjectChildList* virtualChildren() { return children(); }
    virtual const RenderObjectChildList* virtualChildren() const { return children(); }

    virtual const char* renderName() const { return isAnonymous() ? "RenderTableSection (anonymous)" : "RenderTableSection"; }

    virtual bool isTableSection() const { return true; }

    virtual void destroy();

    virtual void layout();

    virtual void removeChild(RenderObject* oldChild);

    virtual int lowestPosition(bool includeOverflowInterior, bool includeSelf) const;
    virtual int rightmostPosition(bool includeOverflowInterior, bool includeSelf) const;
    virtual int leftmostPosition(bool includeOverflowInterior, bool includeSelf) const;

    virtual void paint(PaintInfo&, int tx, int ty);
    virtual void paintObject(PaintInfo&, int tx, int ty);

    virtual void imageChanged(WrappedImagePtr, const IntRect* = 0);

    virtual bool nodeAtPoint(const HitTestRequest&, HitTestResult&, int x, int y, int tx, int ty, HitTestAction);

    virtual int lineHeight(bool, bool) const { return 0; }

    bool ensureRows(int);
    void clearGrid();

    RenderObjectChildList m_children;

    Vector<RowStruct> m_grid;
    Vector<int> m_rowPos;

    int m_gridRows;

    // the current insertion position
    int m_cCol;
    int m_cRow;

    int m_outerBorderLeft;
    int m_outerBorderRight;
    int m_outerBorderTop;
    int m_outerBorderBottom;

    bool m_needsCellRecalc;
    bool m_hasOverflowingCell;
};

inline RenderTableSection* toRenderTableSection(RenderObject* object)
{
    ASSERT(!object || object->isTableSection());
    return static_cast<RenderTableSection*>(object);
}

inline const RenderTableSection* toRenderTableSection(const RenderObject* object)
{
    ASSERT(!object || object->isTableSection());
    return static_cast<const RenderTableSection*>(object);
}

// This will catch anyone doing an unnecessary cast.
void toRenderTableSection(const RenderTableSection*);

} // namespace WebCore

#endif // RenderTableSection_h
