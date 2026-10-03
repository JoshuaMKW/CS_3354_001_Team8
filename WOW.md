# Ways of Working (WoW) — AI Student Planner (Cramberry)

This document outlines the standard operating procedures, development methodology, and communication guidelines for the AI Student Planner project. All team members should adhere to these practices to ensure a smooth, efficient, and collaborative development lifecycle.

## 1. Communication & Meetings

* **Primary Communication Platform:** Microsoft Teams
* All day-to-day project communication, quick technical questions, and general updates will take place in the dedicated Microsoft Teams channels.


* **Weekly Standup & Sync:** Thursdays at 1:00 PM (Virtual)
* **Format:** Virtual meeting via Microsoft Teams.
* **Agenda:** Review sprint progress, discuss blockers, plan upcoming tasks, and review any major architectural decisions.
* **Preparation:** Update task statuses before the meeting to keep the sync focused and efficient.



## 2. Development Methodology (Scrum)

The project will follow an Agile Scrum methodology, enhanced by AI assistance to streamline project management.

* **Sprints:** Development will be broken down into iterative sprints (typically 1–2 weeks).
* **AI-Assisted Scrum Management:**
* **Backlog Grooming:** AI tools will be used to help draft clear user stories, define acceptance criteria, and break down larger epics into manageable tasks.
* **Sprint Planning:** AI will assist in estimating complexity/story points based on historical task data and project scope.
* **Retrospectives:** AI will be utilized to summarize meeting transcripts, categorize feedback (what went well, what didn't), and generate actionable items for the next sprint.



## 3. Technology Stack Guidelines

All development must align with the approved project architecture:

* **Framework:** Next.js (Full-stack web framework)
* **Database:** Supabase PostgreSQL
* **Authentication:** Supabase Auth
* **AI Integration:** Gemini API (strictly using the API; no custom model training)
* **Hosting & CI/CD:** Vercel (Automatic deployments from GitHub)
* **Version Control:** GitHub

## 4. Version Control & Feature Integration

To maintain a stable main codebase, we utilize a strict feature-branch workflow.

### Branching Strategy

* **`main` branch:** The single source of truth. This branch is always deployable and reflects the current production-ready state of the application. Direct commits to `main` are prohibited.
* **Feature Branches:** All new work must take place on a separate branch created from `main`.
* *Naming Convention:* `type/short-description` (e.g., `feature/user-auth`, `bugfix/calendar-sync`, `docs/readme-update`).



### Pull Request (PR) Workflow

1. **Branch Creation:** Pull the latest `main` and create your branch (`git checkout -b feature/task-name`).
2. **Development:** Write your code, ensuring it aligns with the Next.js and Supabase architecture. Commit frequently with clear, descriptive messages.
3. **Draft PR:** Push your branch to GitHub and open a PR against `main`.
4. **Review & CI/CD:**
* Vercel will automatically generate a preview deployment for the PR.
* At least one team member must review and approve the code.


5. **Merge:** Once approved and all automated checks pass, squash and merge the PR into `main`. Delete the feature branch post-merge to keep the repository clean.

## 5. Definition of Done (DoD)

A task or user story is only considered "Done" when:

* [ ] Code is fully written and implements the required functionality.
* [ ] The feature integrates successfully with the Gemini API or Supabase backend (where applicable).
* [ ] Code has been pushed to a separate feature branch and a PR has been created.
* [ ] The PR has been reviewed and approved by a peer.
* [ ] The Vercel preview build compiles and deploys without errors.
* [ ] The code is merged into `main`.
