#include "PaneBase.h"
#include <QTimer>

PaneBase::PaneBase(QWidget* parent) : QWidget(parent) {
	Session().AddObserver(this);
}

PaneBase::~PaneBase() {
	Session().RemoveObserver(this);
}

bool PaneBase::CanRefresh() const {
	auto& s = Session();
	return s.HasTarget() && s.IsBroken();
}

void PaneBase::MarkDirty() {
	m_Dirty = true;
	if (m_Scheduled)
		return;
	// (after the events of the stop are all in: one refresh for all of them)
	m_Scheduled = true;
	auto alive = m_Lifetime.Watch();
	QTimer::singleShot(0, this, [this, alive] {
		if (!*alive)
			return;
		m_Scheduled = false;
		TryRefresh();
	});
}

void PaneBase::TryRefresh() {
	if (!m_Dirty || m_Refreshing || !isVisible() || !CanRefresh())
		return;
	m_Dirty = false;
	m_Refreshing = true;
	RunRefresh();
}

DebugEngine::Task<void> PaneBase::RunRefresh() {
	auto alive = m_Lifetime.Watch();
	co_await Refresh();
	if (!*alive)
		co_return;
	m_Refreshing = false;
	if (m_Dirty)
		TryRefresh();
}

void PaneBase::showEvent(QShowEvent* event) {
	QWidget::showEvent(event);
	TryRefresh();
}

void PaneBase::OnSessionChanged() {
	TryRefresh();
}
