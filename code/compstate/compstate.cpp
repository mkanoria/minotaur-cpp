#include "compstate.h"
#include "objectprocedure.h"
#include "parammanager.h"
#include "procedure.h"

#include "../camera/statusbox.h"
#include "../camera/statuslabel.h"
#include "../gui/global.h"
#include "../utility/logger.h"
#include "../utility/utility.h"
#include "../utility/vector.h"

#include <opencv2/core/types.hpp>

#ifndef NDEBUG
#include <cassert>
#include <QDebug>
#endif

/**
 * Determine the likelihood that a bounding box actually contains the
 * object or robot that is tracked, based on the squareness of the rectangle
 * and its closeness to the calibrated area.
 *
 * Based off formula in
 * https://users.cs.cf.ac.uk/Paul.Rosin/resources/papers/squareness-JMIV-postprint.pdf
 *
 * @param rect            bounding box rectangle
 * @param calibrated_area the expected area of the object or robot
 * @return a value representing accuracy
 */
static double acquisition_r(const cv::Rect2d &rect, double calibrated_area) {
    double area = rect.width * rect.height;
    if (!area) { return 1000; }
    double t = rect.width > rect.height
               ? rect.width / rect.height
               : rect.height / rect.width;
    if (t <= 0.99) { return 1000; }
    return fabs(area - calibrated_area) / (area > calibrated_area ? area : calibrated_area) * t;
}

static QString center_text(const cv::Rect2d &rect, const char *label) {
    QString text;
    text.sprintf("%6s: (%6.1f , %6.1f )", label, rect.x + rect.width / 2, rect.y + rect.height / 2);
    return text;
}

struct CompetitionState::Impl {
    cv::Rect2d box_robot;
    cv::Rect2d box_object;
    cv::Rect2d box_target;
};

CompetitionState::CompetitionState(MainWindow *parent) :
    m_parent(parent),
    m_impl(std::make_unique<Impl>()),
    m_tracking_robot(false),
    m_tracking_object(false),
    m_acquire_walls(false),
    m_object_type(UNACQUIRED) {
    if (auto lp = parent->status_box().lock()) {
        m_robot_loc_label = lp->add_label(center_text(cv::Rect2d(), "Robot"));
        m_object_loc_label = lp->add_label(center_text(cv::Rect2d(), "Object"));
    }
}

CompetitionState::~CompetitionState() = default;

void CompetitionState::acquire_robot_box(const cv::Rect2d &robot_box) {
#ifndef NDEBUG
    assert(m_robot_loc_label != nullptr);
#endif
    m_robot_loc_label->setText(center_text(robot_box, "Robot"));
    m_impl->box_robot = robot_box;
    m_robot_box_fresh = true;
}

void CompetitionState::acquire_object_box(const cv::Rect2d &object_box) {
#ifndef NDEBUG
    assert(m_object_loc_label != nullptr);
#endif
    m_object_loc_label->setText(center_text(object_box, "Object"));
    m_impl->box_object = object_box;
    m_object_box_fresh = true;
}

void CompetitionState::acquire_target_box(const cv::Rect2d &target_box) {
    m_impl->box_target = target_box;
}

void CompetitionState::acquire_walls(std::shared_ptr<wall_arr> &walls) {
    m_walls = walls;
}

bool CompetitionState::is_tracking_robot() const {
    return m_tracking_object;
}

bool CompetitionState::is_tracking_object() const {
    return m_tracking_object;
}

int CompetitionState::object_type() const {
    return m_object_type;
}

void CompetitionState::set_tracking_robot(bool tracking_robot) {
    m_tracking_robot = tracking_robot;
}

void CompetitionState::set_tracking_object(bool tracking_object) {
    m_tracking_object = tracking_object;
}

void CompetitionState::set_object_type(int object_type) {
    m_object_type = object_type;
}

cv::Rect2d &CompetitionState::get_robot_box(bool consume) {
    m_robot_box_fresh = m_robot_box_fresh && !consume;
    return m_impl->box_robot;
}

cv::Rect2d &CompetitionState::get_object_box(bool consume) {
    m_object_box_fresh = m_object_box_fresh && !consume;
    return m_impl->box_object;
}

cv::Rect2d &CompetitionState::get_target_box() {
    return m_impl->box_target;
}

bool CompetitionState::is_robot_box_fresh() const {
    return m_robot_box_fresh;
}

bool CompetitionState::is_object_box_fresh() const {
    return m_object_box_fresh;
}

bool CompetitionState::is_robot_box_valid() const {
    return acquisition_r(m_impl->box_robot, g_pm->robot_calib_area) < g_pm->area_acq_r_sigma;
}

bool CompetitionState::is_object_box_valid() const {
    return acquisition_r(m_impl->box_object, g_pm->object_calib_area) < g_pm->area_acq_r_sigma;
}

void CompetitionState::clear_path() {
    m_path.clear();
}

void CompetitionState::append_path(double x, double y) {
#ifndef NDEBUG
    qDebug() << '(' << x << ',' << ' ' << y << ')';
#endif
    m_path.emplace_back(x, y);
}

const path2d &CompetitionState::get_path() const {
    return m_path;
}

void CompetitionState::begin_traversal() {
    m_procedure = std::make_unique<Procedure>(Main::get()->controller(), m_path);
    m_procedure->start();
}

void CompetitionState::halt_traversal() {
    m_procedure->stop();
}

void CompetitionState::begin_object_move() {
    m_object_procedure = std::make_unique<ObjectProcedure>(Main::get()->controller(), m_path);
    m_object_procedure->start();
}

void CompetitionState::halt_object_move() {
    m_object_procedure->stop();
}
