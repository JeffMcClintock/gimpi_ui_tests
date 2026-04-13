// GMPI-UI Layout Tests
// Tests for the Grid layout container class (gmpi::ui::builder::Grid).
//
// These tests verify Grid's layout calculation algorithm by creating mock views,
// laying them out with Grid, and checking that bounds are calculated correctly.

#include "DrawingTestFixture.h"
#include "experimental/ui_facades.h"

// Separate fixture so layout tests appear in their own group in VS Test Explorer.
class LayoutTest : public DrawingTest {};

// ============================================================
// Stub implementations for View base class methods
// ============================================================

// We link against the real grid.cpp for Grid::doLayout() and Grid::RenderIfDirty().
// We only provide minimal stubs for methods that have dependencies we don't want to pull in.

namespace gmpi::ui::builder
{
    void View::setDirty() { dirty = true; }
    void View::clear2() {}
    void View::OnModelWillChange() { setDirty(); }

    bool View::RenderIfDirty(
        gmpi_forms::Environment*,
        gmpi::forms::primitive::IVisualParent&,
        gmpi::forms::primitive::IMouseParent&
    ) const
    {
        dirty = false;
        return true;
    }

    void RectangleView::Render(gmpi_forms::Environment*, gmpi::forms::primitive::Canvas&) const {}
}

// Stubs for MousePortal methods used by Grid::RenderIfDirty
namespace gmpi::forms::primitive
{
    Interactor* MousePortal::saveMouseState() const { return nullptr; }
    void MousePortal::restoreMouseState(Interactor*) {}
}

// ============================================================
// Builder Wrappers (following library pattern)
// ============================================================

// DisplayList - the supervising object that owns the resulting view hierarchy.
// Builders populate this, then vanish. The test inspects this afterward.
class DisplayList
{
public:
    std::vector<std::unique_ptr<gmpi::ui::builder::View>> children;

    // Helper to get child view
    gmpi::ui::builder::View* operator[](size_t index) const
    {
        return children[index].get();
    }

    size_t size() const { return children.size(); }
};

// Helper to run builders and collect results into a DisplayList.
// Sets up a temporary parent as ThreadLocalCurrentBuilder, runs the body
// (which creates ui::Grid facade -> builder::Grid view), then extracts results.
template<typename F>
DisplayList buildUI(F&& body)
{
    DisplayList result;

    // Test scaffolding: temporary parent to receive the top-level Grid view
    gmpi::ui::builder::ViewParent tempParent;

    auto saveParent = gmpi::ui::builder::ThreadLocalCurrentBuilder;
    gmpi::ui::builder::ThreadLocalCurrentBuilder = &tempParent;

    body();  // ui::Grid creates builder::Grid, adds children, then evaporates

    gmpi::ui::builder::ThreadLocalCurrentBuilder = saveParent;

    // Extract the Grid view that was created and do layout
    if (!tempParent.childViews.empty())
    {
        auto* grid = static_cast<gmpi::ui::builder::Grid*>(tempParent.childViews[0].get());
        grid->doLayout();

        for (auto& child : grid->childViews)
        {
            result.children.push_back(std::move(child));
        }
    }

    return result;
}


// ============================================================
// Grid Layout Algorithm Tests
// ============================================================

// Test Grid with auto-flow rows: items stack vertically with fixed row height.
TEST_F(LayoutTest, GridAutoFlowRows)
{
    using namespace gmpi::ui::builder;

    // Build UI - the lambda contains only "real app" code
    auto displayList = buildUI([&]() {

        gmpi::ui::Grid grid(
            { .gap = 3.f, .auto_rows = 15.f, .auto_columns = 0.f, .auto_flow = ViewParent::eAutoFlow::rows },
            { 4.f, 4.f, 60.f, 60.f }
        );

            gmpi::ui::Rectangle{{}, gmpi::drawing::Colors::Red};
            gmpi::ui::Rectangle{{}, gmpi::drawing::Colors::Green};
            gmpi::ui::Rectangle{{}, gmpi::drawing::Colors::Blue};
    });

    // Access children from the display list
    auto* child1 = displayList[0];
    auto* child2 = displayList[1];
    auto* child3 = displayList[2];

    // Verify child bounds
    EXPECT_FLOAT_EQ(child1->getBounds().top, 4.f);
    EXPECT_FLOAT_EQ(child1->getBounds().bottom, 19.f);
    EXPECT_FLOAT_EQ(child1->getBounds().left, 4.f);
    EXPECT_FLOAT_EQ(child1->getBounds().right, 60.f);

    EXPECT_FLOAT_EQ(child2->getBounds().top, 22.f);
    EXPECT_FLOAT_EQ(child2->getBounds().bottom, 37.f);

    EXPECT_FLOAT_EQ(child3->getBounds().top, 40.f);
    EXPECT_FLOAT_EQ(child3->getBounds().bottom, 55.f);

    // Visualize the result
    g.clear(gmpi::drawing::Colors::White);
    for (size_t i = 0; i < displayList.size(); ++i)
    {
        auto* rv = static_cast<gmpi::ui::builder::RectangleView*>(displayList[i]);
        auto brush = g.createSolidColorBrush(rv->fillColor);
        g.fillRectangle(rv->getBounds(), brush);
    }

    EXPECT_TRUE(checkResult("gridAutoFlowRows"));
}

// Test nested Grids: an outer row-flow Grid containing a nested column-flow Grid.
// The outer Grid assigns bounds to the inner Grid, which then lays out its own children.
TEST_F(LayoutTest, GridNested)
{
    using namespace gmpi::ui::builder;

    // Outer grid: 3 rows, the middle row is itself a grid with 3 columns
    //
    //  +---------------------------+
    //  |  Red                      |  row 0
    //  +--------+--------+--------+
    //  | Yellow | Cyan   | Magenta|  row 1 (nested column grid)
    //  +--------+--------+--------+
    //  |  Blue                     |  row 2
    //  +---------------------------+

    gmpi::ui::builder::ViewParent tempParent;
    auto saveParent = gmpi::ui::builder::ThreadLocalCurrentBuilder;
    gmpi::ui::builder::ThreadLocalCurrentBuilder = &tempParent;

    {
        gmpi::ui::Grid outerGrid(
            { .gap = 2.f, .auto_rows = 20.f, .auto_flow = ViewParent::eAutoFlow::rows },
            { 0.f, 0.f, 64.f, 64.f }
        );

            gmpi::ui::Rectangle{{}, gmpi::drawing::Colors::Red};

            {
                gmpi::ui::Grid innerGrid(
                    { .gap = 2.f, .auto_columns = 20.f, .auto_flow = ViewParent::eAutoFlow::columns }
                );

                    gmpi::ui::Rectangle{{}, gmpi::drawing::Colors::Yellow};
                    gmpi::ui::Rectangle{{}, gmpi::drawing::Colors::Cyan};
                    gmpi::ui::Rectangle{{}, gmpi::drawing::Colors::Magenta};
            }

            gmpi::ui::Rectangle{{}, gmpi::drawing::Colors::Blue};
    }

    gmpi::ui::builder::ThreadLocalCurrentBuilder = saveParent;

    // Layout
    auto* outerGrid = static_cast<gmpi::ui::builder::Grid*>(tempParent.childViews[0].get());
    outerGrid->doLayout();

    // Outer grid children: [0]=Red, [1]=inner Grid, [2]=Blue
    ASSERT_EQ(outerGrid->childViews.size(), 3u);

    auto* red  = outerGrid->childViews[0].get();
    auto* inner = static_cast<gmpi::ui::builder::Grid*>(outerGrid->childViews[1].get());
    auto* blue = outerGrid->childViews[2].get();

    // Outer layout: rows at y=0, 22, 44, each 20px tall, full width
    EXPECT_FLOAT_EQ(red->getBounds().top, 0.f);
    EXPECT_FLOAT_EQ(red->getBounds().bottom, 20.f);
    EXPECT_FLOAT_EQ(red->getBounds().left, 0.f);
    EXPECT_FLOAT_EQ(red->getBounds().right, 64.f);

    EXPECT_FLOAT_EQ(inner->getBounds().top, 22.f);
    EXPECT_FLOAT_EQ(inner->getBounds().bottom, 42.f);

    EXPECT_FLOAT_EQ(blue->getBounds().top, 44.f);
    EXPECT_FLOAT_EQ(blue->getBounds().bottom, 64.f);

    // Inner grid children: 3 columns, 20px wide each, 2px gap
    ASSERT_EQ(inner->childViews.size(), 3u);

    auto* yellow  = inner->childViews[0].get();
    auto* cyan    = inner->childViews[1].get();
    auto* magenta = inner->childViews[2].get();

    EXPECT_FLOAT_EQ(yellow->getBounds().left, 0.f);
    EXPECT_FLOAT_EQ(yellow->getBounds().right, 20.f);
    EXPECT_FLOAT_EQ(yellow->getBounds().top, 22.f);
    EXPECT_FLOAT_EQ(yellow->getBounds().bottom, 42.f);

    EXPECT_FLOAT_EQ(cyan->getBounds().left, 22.f);
    EXPECT_FLOAT_EQ(cyan->getBounds().right, 42.f);

    EXPECT_FLOAT_EQ(magenta->getBounds().left, 44.f);
    EXPECT_FLOAT_EQ(magenta->getBounds().right, 64.f);

    // Visualize
    g.clear(gmpi::drawing::Colors::White);

    auto drawRect = [&](gmpi::ui::builder::View* v) {
        auto* rv = static_cast<gmpi::ui::builder::RectangleView*>(v);
        auto brush = g.createSolidColorBrush(rv->fillColor);
        g.fillRectangle(rv->getBounds(), brush);
    };

    drawRect(red);
    drawRect(yellow);
    drawRect(cyan);
    drawRect(magenta);
    drawRect(blue);

    EXPECT_TRUE(checkResult("gridNested"));
}

// Test Grid with explicit column widths mixing fixed and fractional (fr) sizes.
// CSS equivalent: grid-template-columns: 20px 1fr 1fr;
// In a 64px-wide container with 2px gaps, remaining space = 64 - 20 - 2*2 = 40px,
// split equally between the two 1fr columns = 20px each.
TEST_F(LayoutTest, GridTemplateColumns)
{
    using namespace gmpi::ui::builder;

    gmpi::ui::builder::ViewParent tempParent;
    auto saveParent = gmpi::ui::builder::ThreadLocalCurrentBuilder;
    gmpi::ui::builder::ThreadLocalCurrentBuilder = &tempParent;

    {
        gmpi::ui::Grid grid(
            {
                .gap = 2.f,
                .auto_rows = 20.f,
                .auto_flow = ViewParent::eAutoFlow::columns,
                .column_widths = { 20.f, fr(1), fr(1) }
            },
            { 0.f, 0.f, 64.f, 64.f }
        );

            gmpi::ui::Rectangle{{}, gmpi::drawing::Colors::Red};
            gmpi::ui::Rectangle{{}, gmpi::drawing::Colors::Green};
            gmpi::ui::Rectangle{{}, gmpi::drawing::Colors::Blue};
    }

    gmpi::ui::builder::ThreadLocalCurrentBuilder = saveParent;

    auto* grid = static_cast<gmpi::ui::builder::Grid*>(tempParent.childViews[0].get());
    grid->doLayout();

    auto* col0 = grid->childViews[0].get();
    auto* col1 = grid->childViews[1].get();
    auto* col2 = grid->childViews[2].get();

    // Column 0: fixed 20px, starts at x=0
    EXPECT_FLOAT_EQ(col0->getBounds().left, 0.f);
    EXPECT_FLOAT_EQ(col0->getBounds().right, 20.f);

    // Column 1: 1fr = 20px, starts at x=22
    EXPECT_FLOAT_EQ(col1->getBounds().left, 22.f);
    EXPECT_FLOAT_EQ(col1->getBounds().right, 42.f);

    // Column 2: 1fr = 20px, starts at x=44
    EXPECT_FLOAT_EQ(col2->getBounds().left, 44.f);
    EXPECT_FLOAT_EQ(col2->getBounds().right, 64.f);

    // All rows same height
    EXPECT_FLOAT_EQ(col0->getBounds().top, 0.f);
    EXPECT_FLOAT_EQ(col0->getBounds().bottom, 20.f);
    EXPECT_FLOAT_EQ(col1->getBounds().top, 0.f);
    EXPECT_FLOAT_EQ(col2->getBounds().top, 0.f);

    // Visualize
    g.clear(gmpi::drawing::Colors::White);

    auto drawRect = [&](gmpi::ui::builder::View* v) {
        auto* rv = static_cast<gmpi::ui::builder::RectangleView*>(v);
        auto brush = g.createSolidColorBrush(rv->fillColor);
        g.fillRectangle(rv->getBounds(), brush);
    };

    drawRect(col0);
    drawRect(col1);
    drawRect(col2);

    EXPECT_TRUE(checkResult("gridTemplateColumns"));
}

// Test Grid with weighted fractional columns: 1fr vs 2fr.
// CSS equivalent: grid-template-columns: 1fr 2fr;
// In a 64px-wide container with 2px gap, available = 62px.
// 1fr = 62/3 ≈ 20.667px, 2fr = 124/3 ≈ 41.333px.
TEST_F(LayoutTest, GridFractionalWeights)
{
    using namespace gmpi::ui::builder;

    gmpi::ui::builder::ViewParent tempParent;
    auto saveParent = gmpi::ui::builder::ThreadLocalCurrentBuilder;
    gmpi::ui::builder::ThreadLocalCurrentBuilder = &tempParent;

    {
        gmpi::ui::Grid grid(
            {
                .gap = 2.f,
                .auto_rows = 20.f,
                .auto_flow = ViewParent::eAutoFlow::columns,
                .column_widths = { fr(1), fr(2) }
            },
            { 0.f, 0.f, 64.f, 64.f }
        );

            gmpi::ui::Rectangle{{}, gmpi::drawing::Colors::Red};
            gmpi::ui::Rectangle{{}, gmpi::drawing::Colors::Green};
    }

    gmpi::ui::builder::ThreadLocalCurrentBuilder = saveParent;

    auto* grid = static_cast<gmpi::ui::builder::Grid*>(tempParent.childViews[0].get());
    grid->doLayout();

    auto* col0 = grid->childViews[0].get();
    auto* col1 = grid->childViews[1].get();

    // 1fr = 62/3, 2fr = 124/3
    EXPECT_NEAR(col0->getBounds().left, 0.f, 0.01f);
    EXPECT_NEAR(col0->getBounds().right, 62.f / 3.f, 0.01f);

    EXPECT_NEAR(col1->getBounds().right, 64.f, 0.01f);
    EXPECT_NEAR(col1->getBounds().left, 62.f / 3.f + 2.f, 0.01f);

    // Visualize
    g.clear(gmpi::drawing::Colors::White);

    auto drawRect = [&](gmpi::ui::builder::View* v) {
        auto* rv = static_cast<gmpi::ui::builder::RectangleView*>(v);
        auto brush = g.createSolidColorBrush(rv->fillColor);
        g.fillRectangle(rv->getBounds(), brush);
    };

    drawRect(col0);
    drawRect(col1);

    EXPECT_TRUE(checkResult("gridFractionalWeights"));
}
