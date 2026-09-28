# Statement of Work — AI Student Planner

**Project Title**
AI Student Planner

**Project Overview**
The AI Student Planner is a web-based application designed to help students organize and manage their academic responsibilities. Users will be able to enter information such as courses, assignments, exams, deadlines, and study tasks. The system will use the **Gemini API** to analyze this information and generate a personalized, prioritized study plan.
The planner will also allow students to update or complete tasks. When academic responsibilities change, the system can regenerate the student's plan based on the latest information.

**Project Objective**
The objective of this project is to develop a simple academic planning system that helps students:
*   Keep academic tasks in one location.
*   Identify important upcoming deadlines.
*   Prioritize assignments, exams, and study activities.
*   Generate an AI-assisted study plan.
*   Adjust the plan when tasks or deadlines change.

**Scope of Work**
The project will include:

1.  **User Registration and Authentication**
    *   Create an account.
    *   Log in and log out.
    *   Maintain separate academic information for each user.
2.  **Academic Task Management**
    *   Add courses.
    *   Add assignments.
    *   Add exams.
    *   Add deadlines and other academic tasks.
    *   Edit, delete, and mark tasks as completed.
3.  **AI Planning**
    *   Send relevant academic information to the Gemini API.
    *   Generate a prioritized daily or weekly study plan.
    *   Recommend which tasks the student should work on first.
4.  **Plan Updating**
    *   Regenerate the study plan when tasks are added, completed, or modified.
5.  **Student Dashboard**
    *   Display upcoming assignments and exams.
    *   Display task priorities.
    *   Display the generated study plan.

**Technology Stack**

| Component | Technology |
| :--- | :--- |
| Full-stack web framework | Next.js |
| Database | Supabase PostgreSQL |
| Authentication | Supabase Auth |
| AI functionality | Gemini API |
| Hosting | Vercel |
| Source control | GitHub |

**System Output**
The primary output of the system will be a **personalized academic plan** showing the student what tasks should be completed and their relative priority. 
For example:

```text
Today's Plan

1. Complete CS 3354 Assignment - High Priority
2. Study Algorithms Chapter 6 - High Priority
3. Review Linear Algebra Notes - Medium Priority
4. Prepare Research Meeting Notes - Low Priority
```

**Deliverables**
The team will deliver:
*   A functioning web application.
*   User registration and login.
*   Academic task management.
*   AI-generated study plans using Gemini.
*   A student dashboard.
*   Supabase database integration.
*   GitHub repository containing the project source code.

**Out of Scope**
To keep the project manageable, the initial version will **not** include:
*   Direct university LMS integration such as Canvas or Blackboard.
*   Automatic university calendar synchronization.
*   Native iOS or Android applications.
*   Complex multi-agent AI systems.
*   Training or fine-tuning a custom AI model.

The application will instead use an existing Gemini model through its API.

**Completion Criteria**
The project will be considered complete when a user can:

```text
Register / Login
       ↓
Enter academic tasks
       ↓
Save tasks
       ↓
Generate an AI study plan
       ↓
View prioritized tasks
       ↓
Complete or modify tasks
       ↓
Generate an updated plan
```
